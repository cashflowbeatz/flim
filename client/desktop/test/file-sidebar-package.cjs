'use strict';
const {_electron} = require('playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const {createHash, randomUUID} = require('node:crypto');
const assert = require('node:assert/strict');
const asar = require('@electron/asar');
const fitted = require('./window-geometry.cjs');
const root = path.resolve(__dirname, '..');
const evidence = path.join(__dirname, 'evidence/file-sync');
const digest = data => createHash('sha256').update(data).digest('hex');

(async () => {
  const executablePath = path.join(root, 'release-files/win-unpacked/flim Files Preview.exe');
  const userData = path.join(__dirname, '.tmp/files-package-' + randomUUID());
  await fs.mkdir(evidence, {recursive: true});
  const native = {};
  const archive = path.join(root, 'release-files/win-unpacked/resources/app.asar');
  const runtime = {};
  for (const file of ['electron/main.cjs', 'electron/preload.cjs', 'electron/bridge-core.cjs', 'electron/file-transfers.cjs', 'electron/window-fit.cjs']) {
    const source = await fs.readFile(path.join(root, file)), bundled = asar.extractFile(archive, file);
    assert.equal(digest(bundled), digest(source)); runtime[file] = digest(bundled);
  }
  const renderer = await fs.readFile(path.join(root, 'dist-files/index.html'));
  assert.equal(digest(asar.extractFile(archive, 'dist/index.html')), digest(renderer));
  for (const file of await fs.readdir(path.join(root, 'dist-files/assets'))) {
    assert.equal(digest(asar.extractFile(archive, path.join('dist', 'assets', file))), digest(await fs.readFile(path.join(root, 'dist-files/assets', file))));
  }
  for (const file of ['flim.exe', 'flim_engine.dll', 'profiles/25.2.5.5319.profile', 'profiles/26.1.7.5654.profile']) {
    const source = await fs.readFile(path.join(root, '../out-file-sync', file));
    const bundled = await fs.readFile(path.join(root, 'release-files/win-unpacked/resources/native', file));
    assert.equal(digest(bundled), digest(source)); native[file] = digest(bundled);
  }
  const app = await _electron.launch({executablePath, args: ['--user-data-dir=' + userData], cwd: root});
  const errors = [], checks = [];
  try {
    const page = await app.firstWindow(); page.on('pageerror', error => errors.push(String(error)));
    await page.getByRole('button', {name: 'Get started', exact: true}).waitFor();
    const initial = await page.evaluate(async () => ({state: await window.flimDesktop.getState(), files: await window.flimDesktop.getFileTransfers()}));
    assert.equal(initial.state.nativeReady, true);
    assert.deepEqual(initial.files, {items: [], available: true, stale: false});
    checks.push('Packaged renderer, Electron modules and native payload match the current build.');
    checks.push('Real preload and trusted IPC return an empty file list before any native launch.');

    // Only room display is simulated. No room is created and no FL process is launched.
    const displayState = {...initial.state, room: {code: '12345678', role: 'host', maxParticipants: 8, expiresAt: null, state: 'launched', status: 'active', peers: []}};
    await app.evaluate(({ipcMain}, state) => {
      for (const channel of ['flim:get-state', 'flim:refresh-room']) {
        ipcMain.removeHandler(channel); ipcMain.handle(channel, () => state);
      }
    }, displayState);
    await page.reload(); await page.getByRole('button', {name: 'Files', exact: true}).waitFor();
    const collapsed = await fitted(app, page);
    await page.getByRole('button', {name: 'Files', exact: true}).click();
    await page.getByText('Add a sample to a shared channel or playlist', {exact: false}).waitFor();
    const expanded = await fitted(app, page);
    assert.ok(expanded.native.bounds.width > collapsed.native.bounds.width + 250);
    await page.screenshot({path: path.join(evidence, 'packaged-files.png'), fullPage: true});
    checks.push('Files expands the real native window and receives its empty state through actual IPC.');
    await app.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(2));
    const zoomed = await fitted(app, page); assert.equal(zoomed.native.zoom, 2);
    await page.getByRole('button', {name: 'Hide files', exact: true}).scrollIntoViewIfNeeded();
    await page.getByRole('button', {name: 'Open samples folder', exact: true}).scrollIntoViewIfNeeded();
    const capture = await app.evaluate(async ({BrowserWindow}) => (await BrowserWindow.getAllWindows()[0].webContents.capturePage()).toPNG().toString('base64'));
    await fs.writeFile(path.join(evidence, 'packaged-files-zoom2.png'), Buffer.from(capture, 'base64'));
    await page.getByRole('button', {name: 'Hide files', exact: true}).focus(); await page.keyboard.press('Escape');
    await page.waitForFunction(width => innerWidth < width, zoomed.renderer.width);
    const hiddenZoomed = await fitted(app, page);
    assert.ok(hiddenZoomed.native.bounds.width < zoomed.native.bounds.width);
    assert.equal(await page.getByRole('button', {name: 'Files', exact: true}).evaluate(el => el === document.activeElement), true);
    await app.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(1));
    await page.waitForFunction(width => Math.abs(innerWidth - width) < 2, collapsed.renderer.width);
    const restored = await fitted(app, page);
    assert.equal(restored.native.bounds.width, collapsed.native.bounds.width);
    checks.push('At 200% native zoom the drawer reflows, remains reachable, and Escape restores focus and window width.');
    assert.deepEqual(errors, []);
    await fs.writeFile(path.join(evidence, 'package.json'), JSON.stringify({passed: true, checks, native, runtime, errors,
      bounds: {collapsed, expanded, zoomed, hiddenZoomed, restored},
      scope: 'Packaged Electron runtime, isolated user-data directory, real native inspection and file IPC. Room display is a fixture. No live FL sharing tested.'}, null, 2));
    checks.forEach(check => console.log('PASS ' + check));
  } finally {await app.close();}
})().catch(error => {console.error(error); process.exitCode = 1;});
