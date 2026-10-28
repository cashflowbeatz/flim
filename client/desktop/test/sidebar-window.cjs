const {_electron} = require('playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');
const fitted = require('./window-geometry.cjs');
const root = path.resolve(__dirname, '..');

(async () => {
  const temporaryRoot = path.join(__dirname, '.tmp'); await fs.mkdir(temporaryRoot, {recursive: true});
  const directory = await fs.mkdtemp(path.join(temporaryRoot, 'sidebar-window-'));
  const bootstrap = path.join(directory, 'bootstrap.cjs');
  await fs.writeFile(bootstrap, `
    const {app, BrowserWindow} = require('electron');
    global.layoutBounds = [];
    const setBounds = BrowserWindow.prototype.setBounds;
    BrowserWindow.prototype.setBounds = function(bounds, ...args) {global.layoutBounds.push(bounds); return setBounds.call(this, bounds, ...args);};
    require('node:fs').mkdirSync(${JSON.stringify(path.join(directory, 'profile'))}, {recursive:true});
    app.setPath('userData', ${JSON.stringify(path.join(directory, 'profile'))});
    const {DesktopBridge} = require(${JSON.stringify(path.join(root, 'electron/bridge-core.cjs'))});
    const room = {code:'12345678',role:'member',maxParticipants:8,expiresAt:null,state:'launched',status:'active',peers:[
      {id:'a',name:'Layout fixture',role:'host',fl:'25.2.5.5319'},{id:'b',name:'Sidebar fixture',role:'member',fl:'26.1.7.5654'}]};
    const state = {platform:'win32',name:'Sidebar fixture',installs:[],selectedInstallId:'',room,busy:false,nativeReady:true};
    DesktopBridge.prototype.initialize = async function() { this.room = room; };
    DesktopBridge.prototype.state = function() { return structuredClone(state); };
    DesktopBridge.prototype.refreshRoom = async function() { return structuredClone(state); };
    const {FileTransfers} = require(${JSON.stringify(path.join(root, 'electron/file-transfers.cjs'))});
    FileTransfers.prototype.snapshot = async () => ({available:true,stale:false,items:[
      {id:'fixture',name:'layout-fixture.wav',direction:'download',state:'waiting',reason:'retrying',bytes:65536,done:0,updatedAt:Date.now()}]});
    require(${JSON.stringify(path.join(root, 'electron/main.cjs'))});
  `);
  let app;
  try {
    app = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron.exe'), args: [bootstrap], cwd: root});
    const page = await app.firstWindow();
    await page.getByRole('button', {name: 'Files', exact: true}).waitFor();
    const closed = await fitted(app, page);
    await app.evaluate(() => {global.layoutBounds = [];});
    await page.getByRole('button', {name: 'Files', exact: true}).click();
    await page.getByText('layout-fixture.wav', {exact: true}).waitFor();
    await page.waitForFunction(() => innerWidth === 680);
    const open = await fitted(app, page);
    const openingBounds = await app.evaluate(() => global.layoutBounds);
    assert.ok(openingBounds.length > 0);
    assert.ok(openingBounds.every(bounds => bounds.height >= closed.native.bounds.height - 2), JSON.stringify({closed, openingBounds}));
    assert.equal(open.native.content[0], 680);
    const side = await page.locator('.files-sidebar').boundingBox(), roomBounds = await page.locator('.card-wrap').boundingBox();
    assert.ok(side.x > roomBounds.x + roomBounds.width && Math.abs(side.y - roomBounds.y) < 2);
    assert.equal(await page.getByText('Layout fixture', {exact: true}).isVisible(), true);
    assert.equal(await page.getByRole('button', {name: 'Return to start', exact: true}).isEnabled(), true);
    const evidence = path.join(__dirname, 'evidence', require('../package.json').version); await fs.mkdir(evidence, {recursive: true});
    const capture = async name => fs.writeFile(path.join(evidence, name), Buffer.from(await app.evaluate(async ({BrowserWindow}) =>
      (await BrowserWindow.getAllWindows()[0].webContents.capturePage()).toPNG().toString('base64')), 'base64'));
    await capture('native-sidebar.png');
    await app.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(2));
    const zoom = await fitted(app, page); assert.ok(zoom.renderer.scroll <= zoom.renderer.width);
    await page.getByRole('button', {name: 'Hide files', exact: true}).scrollIntoViewIfNeeded();
    await capture('native-sidebar-zoom.png');
    await app.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(1)); await fitted(app, page);
    await page.getByRole('button', {name: 'Hide files', exact: true}).focus(); await page.keyboard.press('Escape');
    await page.waitForFunction(() => innerWidth === 400);
    const restored = await fitted(app, page); assert.equal(restored.native.content[0], closed.native.content[0]);
    assert.equal(await page.getByRole('button', {name: 'Files', exact: true}).evaluate(el => el === document.activeElement), true);
    await fs.writeFile(path.join(evidence, 'sidebar-window.json'), JSON.stringify({passed: true, closed, open, openingBounds, zoom, restored,
      scope: 'Real Electron window with isolated room/file fixtures. No room allocation or FL process launch.'}, null, 2));
    console.log('PASS real Electron sidebar aligns beside the room, fits content, supports 200% zoom and restores focus/width');
  } finally {
    await app?.close();
    assert.ok(directory.startsWith(temporaryRoot + path.sep)); await fs.rm(directory, {recursive: true, force: true});
  }
})().catch(error => {console.error(error); process.exitCode = 1;});
