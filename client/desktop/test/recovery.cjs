const {_electron} = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs/promises');
const root = path.resolve(__dirname, '..');

(async () => {
  const parent = path.join(__dirname, '.tmp'); await fs.mkdir(parent, {recursive: true});
  const profile = await fs.mkdtemp(path.join(parent, 'recovery-'));
  const app = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron.exe'), args: [root, '--user-data-dir=' + profile], cwd: root});
  try {
    const page = await app.firstWindow();
    await page.getByRole('button', {name: 'Get started', exact: true}).waitFor();
    const before = await page.evaluate(() => window.flimDesktop.getState());
    const read = source => app.evaluate(({BrowserWindow}, source) => BrowserWindow.getAllWindows()[0].webContents.executeJavaScript(source), source);
    const ready = async selector => {
      for(let attempt=0;attempt<100;attempt++) {
        if(await read(`Boolean(document.querySelector(${JSON.stringify(selector)}))`).catch(()=>false))return;
        await new Promise(resolve=>setTimeout(resolve,50));
      }
      assert.fail('Recovered controls did not become ready: '+selector);
    };
    const pid = await app.evaluate(() => process.pid);
    await assert.rejects(page.evaluate(() => window.flimDesktop.reloadControls()), /only available from the recovery screen/);
    for (let crash = 0; crash < 3; crash++) {
      await app.evaluate(async ({BrowserWindow}) => {
        const contents = BrowserWindow.getAllWindows()[0].webContents;
        await new Promise(resolve => { contents.once('did-finish-load', resolve); contents.forcefullyCrashRenderer(); });
      });
      if (crash < 2) {
        await ready('.start-button');
        const after = await read('window.flimDesktop.getState()');
        assert.deepEqual(after, before);
      } else await ready('#reload');
      assert.equal(await app.evaluate(() => process.pid), pid);
    }
    const destination = path.join(__dirname, 'evidence', require('../package.json').version);
    await fs.mkdir(destination, {recursive: true});
    const png=await app.evaluate(async({BrowserWindow})=>(await BrowserWindow.getAllWindows()[0].webContents.capturePage()).toPNG().toString('base64'));
    await fs.writeFile(path.join(destination, 'renderer-recovery.png'),Buffer.from(png,'base64'));
    await app.evaluate(async ({BrowserWindow}) => {
      const contents=BrowserWindow.getAllWindows()[0].webContents;
      await new Promise(resolve=>{contents.once('did-finish-load',resolve);void contents.executeJavaScript("document.querySelector('#reload').click()");});
    });
    await ready('.start-button');
    assert.deepEqual(await read('window.flimDesktop.getState()'), before);
    await app.close();
    const sessions = await fs.readdir(path.join(profile, 'logs'));
    const events = (await fs.readFile(path.join(profile, 'logs', sessions[0], 'events.jsonl'), 'utf8')).trim().split('\n').map(JSON.parse);
    const recoveries = events.filter(event => event.event === 'renderer_recovery');
    assert.deepEqual(recoveries.map(event => event.state), ['automatic', 'automatic', 'manual']);
    await fs.writeFile(path.join(destination, 'recovery-results.json'), JSON.stringify({passed: true, checks: ['Two real renderer crashes reload automatically and preserve the main PID and bridge state', 'A third crash opens a bounded recovery screen', 'Recovery button restores the controls without a native launch', 'Normal renderer cannot invoke recovery-only IPC', 'All recoveries recorded in local diagnostics'], recoveries}, null, 2));
    console.log('PASS actual Electron crashes recover twice, stop the loop, reload on request and retain bridge state');
  } finally {
    await app.close().catch(() => {});
    assert.ok(path.resolve(profile).startsWith(path.resolve(parent) + path.sep));
    await fs.rm(profile, {recursive: true, force: true, maxRetries: 8, retryDelay: 250});
  }
})().catch(error => {console.error(error); process.exitCode = 1;});
