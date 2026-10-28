const {_electron} = require('playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
const fitted = require('./window-geometry.cjs');
const root = path.resolve(__dirname, '..');

(async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'flim-location-ui-'));
  const profile = path.join(directory, 'profile'), received = path.join(directory, 'Received audio'), documents = path.join(directory, 'Redirected Documents');
  for (const folder of [profile, received, documents]) await fs.mkdir(folder);
  await fs.writeFile(path.join(received, 'keep.wav'), 'original');
  const bootstrap = path.join(directory, 'bootstrap.cjs');
  await fs.writeFile(bootstrap, `
    const {app, dialog, shell} = require('electron');
    app.setPath('userData', ${JSON.stringify(profile)}); app.setPath('documents', ${JSON.stringify(documents)});
    global.dialogRequests = []; global.openedFolders = []; global.chosenFolder = ${JSON.stringify(received)};
    dialog.showOpenDialog = async (_window, options) => {global.dialogRequests.push(options); return {canceled:false,filePaths:[global.chosenFolder]};};
    shell.openPath = async directory => {global.openedFolders.push(directory); if (global.signalOpen) {global.signalOpen(); global.signalOpen = null;} if (global.deferOpen) await new Promise(resolve => global.releaseOpen = resolve); return '';};
    require(${JSON.stringify(path.join(root, 'electron/main.cjs'))});
  `);
  let app;
  try {
    app = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron.exe'), args: [bootstrap], cwd: root});
    const page = await app.firstWindow(); const errors = []; page.on('pageerror', error => errors.push(String(error)));
    await page.getByRole('button', {name: 'Get started', exact: true}).click(); await page.getByLabel('Your name').fill('Keep my name');
    await page.getByRole('button', {name: 'Files', exact: true}).click(); await page.getByRole('button', {name: 'Choose folder', exact: true}).click();
    await page.getByText(received, {exact: true}).waitFor(); await fitted(app, page);
    const saved = JSON.parse(await fs.readFile(path.join(profile, 'preferences.json'), 'utf8')); assert.equal(saved.sampleDirectory, await fs.realpath(received));
    assert.deepEqual(await fs.readdir(received), ['keep.wav']); assert.equal(await fs.readFile(path.join(received, 'keep.wav'), 'utf8'), 'original');
    assert.equal((await page.evaluate(() => window.flimDesktop.getState())).room, null);
    const request = await app.evaluate(() => global.dialogRequests[0]); assert.equal(request.title, 'Save received shared files to'); assert.ok(request.properties.includes('openDirectory'));
    await page.getByRole('button', {name: 'Open samples folder', exact: true}).click();
    assert.deepEqual(await app.evaluate(() => global.openedFolders), [received]);
    await app.evaluate(() => {global.deferOpen = true;});
    await page.evaluate(() => {window.opening = Promise.all([window.flimDesktop.openSampleFolder(), window.flimDesktop.openSampleFolder()]);});
    await app.evaluate(() => {global.releaseOpen(); global.deferOpen = false;});
    await page.evaluate(() => window.opening); assert.deepEqual(await app.evaluate(() => global.openedFolders), [received, received]);
    await app.evaluate(() => {global.deferOpen = true; global.waitForOpen = new Promise(resolve => {global.signalOpen = resolve;});});
    await page.evaluate(() => {window.logsOpening = Promise.all([window.flimDesktop.openLogs(), window.flimDesktop.openLogs()]);});
    await app.evaluate(() => global.waitForOpen);
    await app.evaluate(() => {global.releaseOpen(); global.deferOpen = false;}); await page.evaluate(() => window.logsOpening);
    const openedLogs = await app.evaluate(() => global.openedFolders.filter(value => value !== global.chosenFolder)); assert.equal(openedLogs.length, 1);
    const restoredWindow = await app.evaluate(({app, BrowserWindow}) => {const window = BrowserWindow.getAllWindows()[0]; window.hide(); app.emit('second-instance'); return window.isVisible();});
    assert.equal(restoredWindow, true);
    await page.getByRole('button', {name: 'Use default', exact: true}).click();
    const defaultDirectory = path.join(documents, 'flim', 'samples'); await page.getByText(defaultDirectory, {exact: true}).waitFor();
    assert.equal(Object.hasOwn(JSON.parse(await fs.readFile(path.join(profile, 'preferences.json'), 'utf8')), 'sampleDirectory'), false);
    assert.deepEqual(await fs.readdir(defaultDirectory), []);
    await page.getByRole('button', {name: 'Hide files', exact: true}).click(); assert.equal(await page.getByLabel('Your name').inputValue(), 'Keep my name');
    await page.getByRole('button', {name: 'Files', exact: true}).click();
    await app.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(2)); await fitted(app, page);
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    const open = page.getByRole('button', {name: 'Open samples folder', exact: true}); await open.scrollIntoViewIfNeeded();
    const box = await open.boundingBox(); assert.ok(box.y >= 0 && box.y + box.height <= await page.evaluate(() => innerHeight) + 1);
    const evidence = path.join(root, 'test/evidence', require('../package.json').version); await fs.mkdir(evidence, {recursive:true});
    const png = await app.evaluate(async ({BrowserWindow}) => (await BrowserWindow.getAllWindows()[0].webContents.capturePage()).toPNG().toString('base64'));
    await fs.writeFile(path.join(evidence, 'native-folder-zoom.png'), Buffer.from(png, 'base64'));
    assert.deepEqual(errors, []);
    await fs.writeFile(path.join(evidence, 'sample-location-window.json'), JSON.stringify({passed:true, dialog:true, persistentChoice:true, defaultUsesRedirectedDocuments:true, noMovedFiles:true, oneOpenForConcurrentRequests:true, oneLogOpenForConcurrentRequests:true, secondInstanceShowsHiddenControls:true, typedNameRetained:true, zoom:2, errors}, null, 2));
    console.log('PASS actual Electron folder dialog bridge, persistence, custom/default opening, concurrent opens, retained form and 200% zoom');
  } finally {
    if (app) await app.close();
    assert.ok(directory.startsWith(os.tmpdir() + path.sep)); await fs.rm(directory, {recursive:true, force:true});
  }
})().catch(error => {console.error(error); process.exitCode = 1;});
