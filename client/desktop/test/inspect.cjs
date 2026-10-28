const { _electron } = require('playwright');
const path = require('node:path');
const fs = require('node:fs/promises');

(async () => {
  const appPath = path.resolve(__dirname, '..');
  const app = await _electron.launch({executablePath: path.join(appPath, 'node_modules/electron/dist/electron.exe'), args: [appPath, `--user-data-dir=${path.join(appPath, 'test/.tmp/inspect-user-data')}`], cwd: appPath});
  try {
    const page = await app.firstWindow();
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    page.on('console', e => { if (['error', 'warning'].includes(e.type())) errors.push(e.text()); });
    await page.getByRole('button', {name: 'Get started', exact: true}).waitFor();
    await page.evaluate(() => document.fonts.ready);
    await fs.mkdir(path.join(__dirname, 'evidence'), {recursive: true});
    await page.screenshot({path: path.join(__dirname, 'evidence/desktop-closed.png')});
    await page.getByRole('button', {name: 'Get started', exact: true}).click();
    await page.getByLabel('Your name').waitFor();
    await page.waitForFunction(() => !document.querySelector('#fl-install').disabled);
    await page.screenshot({path: path.join(__dirname, 'evidence/desktop-open.png'), fullPage: true});
    console.log(JSON.stringify({text: await page.locator('body').innerText(), errors, font: await page.evaluate(() => ({loaded:document.fonts.check('14px "General Sans"'), family:getComputedStyle(document.querySelector('h2')).fontFamily}))}, null, 2));
  } finally { await app.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
