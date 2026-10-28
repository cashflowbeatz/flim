'use strict';

const { app, BrowserWindow, clipboard, dialog, ipcMain, session, shell, screen } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');
const { createHash } = require('node:crypto');
const { pathToFileURL } = require('node:url');
const { DesktopBridge, BridgeError, trustedSender, RELAY } = require('./bridge-core.cjs');
const { createDiagnostics } = require('./diagnostics.cjs');
const { fitBounds } = require('./window-fit.cjs');
const { createRecoveryBudget } = require('./renderer-recovery.cjs');
const { FileTransfers } = require('./file-transfers.cjs');
const { createSetupGate } = require('./setup.cjs');
const {singleFlight} = require('./single-flight.cjs');
const {createPeerFiles} = require('./peer-electron.cjs');
const recoveryURL = pathToFileURL(path.join(__dirname, 'recovery.html')).href;

const dev = !app.isPackaged && process.env.FLIM_DESKTOP_DEV === '1';
const rendererURL = dev ? 'http://127.0.0.1:5173/' : pathToFileURL(path.join(__dirname, '../dist/index.html')).href;
const iconPath = path.join(__dirname, dev ? '../public/brand/flim.ico' : '../dist/brand/flim.ico');
if (process.platform === 'win32') app.setAppUserModelId('com.cashflowbeatz.flim');
let window;
let bridge;
let diagnostics;
let sampleFolderFlight;
let quitFlushed = false;
let peerFiles;
const record = (event, fields) => diagnostics?.record(event, fields);
async function flushDiagnostics() {
  let timer;
  try { await Promise.race([diagnostics.flush(), new Promise(resolve => { timer = setTimeout(resolve, 2000); })]); }
  finally { clearTimeout(timer); }
}
process.on('uncaughtExceptionMonitor', error => { void record('process_error', {message: error.message, stack: error.stack}); });

if (!app.requestSingleInstanceLock()) app.quit();
else {
  app.on('second-instance', () => { if (window && !window.isDestroyed()) { if (window.isMinimized()) window.restore(); window.show(); window.focus(); } });
  app.whenReady().then(async () => {
    diagnostics = await createDiagnostics({rootDir: path.join(app.getPath('userData'), 'logs'), appVersion: app.getVersion()});
    void record('app_start', {appVersion: app.getVersion(), pid: process.pid});
    const nativeDir = app.isPackaged ? path.join(process.resourcesPath, 'native') : path.join(__dirname, '../../out-experimental');
    try {
      const [launcher, engine] = await Promise.all(['flim.exe', 'flim_engine.dll'].map(file => fs.readFile(path.join(nativeDir, file))));
      void record('native_files', {launcherHash: createHash('sha256').update(launcher).digest('hex'), engineHash: createHash('sha256').update(engine).digest('hex')});
    } catch { void record('native_files_missing', {message: 'Bundled native files could not be read.'}); }
    session.defaultSession.setPermissionRequestHandler((_webContents, _permission, callback) => callback(false));
    session.defaultSession.setPermissionCheckHandler(() => false);
    session.defaultSession.webRequest.onHeadersReceived((details, callback) => {
      callback({ responseHeaders: { ...details.responseHeaders, 'Content-Security-Policy': [dev
        ? "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'self' ws://127.0.0.1:5173; object-src 'none'; base-uri 'none'; frame-src 'none'"
        : "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'none'; object-src 'none'; base-uri 'none'; frame-src 'none'"] } });
    });
    const fileTransfers = new FileTransfers(path.join(app.getPath('userData'), 'files'), fs, record);
    peerFiles = createPeerFiles({BrowserWindow, ipcMain, session, relay: RELAY, record,
      idle: () => {if (!quitFlushed && !peerFiles.active && window && !window.isVisible()) app.quit();}});
    bridge = new DesktopBridge({
      fileTransfers, peerFiles,
      nativeDir, record, nativeLogDir: diagnostics.available ? diagnostics.nativeDirectory : '', diagnosticsStatus: () => diagnostics.available,
      preferencesFile: path.join(app.getPath('userData'), 'preferences.json'),
      defaultSampleDirectory: path.join(app.getPath('documents'), 'flim', 'samples'),
      chooseSampleDirectory: async defaultPath => {
        const result = await dialog.showOpenDialog(window, {title: 'Save received shared files to', defaultPath, buttonLabel: 'Save files here', properties: ['openDirectory', 'createDirectory']});
        return result.canceled ? null : result.filePaths[0];
      },
      chooseDirectory: async defaultPath => {
        const result = await dialog.showOpenDialog(window, { title: 'Choose your FL Studio folder', defaultPath, buttonLabel: 'Use this folder', properties: ['openDirectory'] });
        return result.canceled ? null : result.filePaths[0];
      },
      copyText: text => clipboard.writeText(text),
      readText: () => clipboard.readText(),
    });
    const ready = createSetupGate(() => bridge.initialize());
    void ready().catch(error => { void record('startup_failed', {message: error?.message, stack: error?.stack}); });
    ipcMain.handle('flim:reload-controls', async event => {
      if (!window || !trustedSender(event, window.webContents, recoveryURL)) throw new Error('Reload is only available from the recovery screen.');
      setImmediate(() => { if (window && !window.isDestroyed()) void window.loadURL(rendererURL).catch(() => {}); });
    });
    const methods = {
      'get-state': () => bridge.state(),
      'get-file-transfers': () => fileTransfers.snapshot(bridge.room),
      'get-sample-location': () => bridge.getSampleLocation(),
      'choose-sample-location': () => bridge.changeSampleLocation(),
      'reset-sample-location': () => bridge.changeSampleLocation(true),
      'open-sample-folder': async () => {
        if (sampleFolderFlight) return sampleFolderFlight;
        const flight = (async () => {
          const directory = await bridge.checkedSampleDirectory({writable: false});
          if (await shell.openPath(directory)) throw new BridgeError('The shared samples folder could not be opened.');
          return {opened: true};
        })();
        sampleFolderFlight = flight;
        try {return await flight;} finally {if (sampleFolderFlight === flight) sampleFolderFlight = null;}
      },
      'save-preferences': input => bridge.savePreferences(input),
      'choose-install': input => bridge.chooseInstall(input),
      'rescan-installs': input => bridge.rescanInstalls(input),
      'create-room': input => bridge.createRoom(input),
      'join-room': input => bridge.joinRoom(input),
      'launch-fl': () => bridge.launchFL(),
      'refresh-room': input => bridge.refreshRoom(input),
      'leave-room': () => bridge.leaveRoom(),
      'copy-invite': () => bridge.copyInvite(),
      'paste-room-code': () => bridge.pasteRoomCode(),
      'open-logs': singleFlight(async () => {
        if (!diagnostics.available) throw new BridgeError('Test logs could not be saved. Check available disk space and folder access.');
        await flushDiagnostics();
        const error = await shell.openPath(diagnostics.directory);
        if (error) throw new BridgeError('The test logs folder could not be opened.');
        return {opened: true};
      }),
    };
    for (const [name, method] of Object.entries(methods)) {
      ipcMain.handle(`flim:${name}`, async (event, input) => {
        if (!window || !trustedSender(event, window.webContents, rendererURL)) throw new Error('This request did not come from the flim window.');
        try {
          if (name !== 'open-logs') await ready();
          return await method(input);
        }
        catch (error) {
          void record('ipc_error', {operation: name, code: error?.code, message: error?.message, stack: error instanceof BridgeError ? undefined : error?.stack});
          throw new Error(error instanceof BridgeError ? error.message : 'That step could not be completed. Please try again.');
        }
      });
    }
    ipcMain.on('flim:renderer-error', (event, input) => {
      if (!window || !trustedSender(event, window.webContents, rendererURL) || !input || typeof input.message !== 'string') return;
      void record('renderer_error', {message: input.message.slice(0, 2048), stack: typeof input.stack === 'string' ? input.stack.slice(0, 4096) : undefined});
    });
    ipcMain.on('flim:fit-window', (event, height, width = 456) => {
      if (!window || !trustedSender(event, window.webContents, rendererURL)) return;
      const bounds = window.getBounds();
      const next = fitBounds(height, bounds, window.getContentSize(), screen.getDisplayMatching(bounds).workArea, window.webContents.getZoomFactor(), width);
      if (next && (next.width !== bounds.width || next.height !== bounds.height || next.x !== bounds.x || next.y !== bounds.y)) window.setBounds(next);
    });
    const initialArea = screen.getPrimaryDisplay().workArea;
    window = new BrowserWindow({
      title: 'flim', width: 400, height: 340, minWidth: 280, minHeight: 280, maximizable: false, fullscreenable: false, backgroundColor: '#ffffff', show: false,
      x: initialArea.x + Math.max(0, Math.floor((initialArea.width - 400) / 2)), y: initialArea.y + 24,
      titleBarStyle: 'hidden', titleBarOverlay: {color: '#ffffff', symbolColor: '#4b4943', height: 44},
      autoHideMenuBar: true, icon: iconPath,
      webPreferences: { preload: path.join(__dirname, 'preload.cjs'), contextIsolation: true, nodeIntegration: false, sandbox: true, webviewTag: false, devTools: dev },
    });
    window.removeMenu();
    window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
    window.webContents.on('will-navigate', event => event.preventDefault());
    window.webContents.on('will-attach-webview', event => event.preventDefault());
    const canRecover = createRecoveryBudget();
    let recoveryTimer;
    const recover = reason => {
      clearTimeout(recoveryTimer);
      const retry = canRecover();
      void record('renderer_recovery', {reason, state: retry ? 'automatic' : 'manual'});
      recoveryTimer = setTimeout(() => {
        if (!window || window.isDestroyed()) return;
        window.show();
        void window.loadURL(retry ? rendererURL : recoveryURL).catch(() => {});
      }, 300);
    };
    window.webContents.on('render-process-gone', (_event, details) => {
      void record('renderer_exit', {reason: details.reason, exitCode: details.exitCode});
      if (details.reason !== 'clean-exit') recover(details.reason);
    });
    window.webContents.on('did-fail-load', (_event, code, description, url, isMainFrame) => {
      if (!isMainFrame || code === -3) return;
      void record('renderer_load_failed', {code, message: description});
      if (url !== recoveryURL) recover('load-failed');
    });
    window.once('ready-to-show', () => window.show());
    window.on('close', event => {if (!quitFlushed && peerFiles.active) {event.preventDefault(); window.hide();}});
    window.on('closed', () => { clearTimeout(recoveryTimer); window = null; if (!peerFiles.active) app.quit(); });
    await window.loadURL(rendererURL).catch(() => {});
  }).catch(error => {
    void record('startup_failed', {message: error?.message, stack: error?.stack});
    dialog.showErrorBox('flim could not open', 'The application files could not be loaded. Extract the complete app and try again.');
    app.quit();
  });
  app.on('window-all-closed', () => app.quit());
  app.on('child-process-gone', (_event, details) => { void record('child_exit', {reason: details.reason, exitCode: details.exitCode}); });
  app.on('before-quit', event => {
    if (quitFlushed || !diagnostics) return;
    event.preventDefault(); quitFlushed = true;
    void record('app_exit', {pid: process.pid});
    void Promise.allSettled([flushDiagnostics(), peerFiles?.close()]).finally(() => app.quit());
  });
}
