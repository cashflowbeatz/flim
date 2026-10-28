const {_electron} = require('playwright');
const path = require('node:path');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const waitFit = require('./window-geometry.cjs');
const root = path.resolve(__dirname,'..');
(async()=>{
  const app=await _electron.launch({executablePath:path.join(root,'node_modules/electron/dist/electron.exe'),args:[root,'--user-data-dir='+path.join(__dirname,'.tmp/window-fit')],cwd:root});
  try {
    const page=await app.firstWindow();
    await page.getByRole('button',{name:'Get started',exact:true}).waitFor();
    const fitted=()=>waitFit(app,page);
    const closed=await fitted(); console.log('CLOSED',JSON.stringify(closed));
    await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(2));
    const closedZoom=await fitted(); assert.equal(closedZoom.native.zoom,2); assert.ok(closedZoom.native.bounds.width>closed.native.bounds.width*1.5);
    await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(1)); await fitted();
    await page.getByRole('button',{name:'Get started',exact:true}).click();
    await page.waitForFunction(()=>!document.querySelector('#fl-install').disabled);
    const open=await fitted(); assert.ok(open.native.bounds.height>closed.native.bounds.height+100);console.log('OPEN',JSON.stringify(open));
    assert.equal(open.native.bounds.y, closed.native.bounds.y, 'Opening setup keeps the initial upper edge anchored');
    await page.getByLabel('Your name').fill('Native layout fixture');
    for (let i = 0; i < 5; i++) {
      await page.getByRole('tab', {name: 'Join a room', exact: true}).click();
      await page.keyboard.type(i === 0 ? '12345678' : '');
      const joined = await fitted(); assert.deepEqual(joined.native.bounds, open.native.bounds);
      assert.equal(await page.getByLabel('Room number').inputValue(), i === 0 ? '12345678' : '1234 5678');
      await page.getByRole('tab', {name: 'Create a room', exact: true}).click();
      const created = await fitted(); assert.deepEqual(created.native.bounds, open.native.bounds);
    }
    console.log('PASS real native window remains at the same size and position across ten mode switches; immediate typing is retained');
    const picker=page.getByRole('combobox',{name:'FL Studio',exact:true}); await picker.click();
    const dropdown=await fitted(); assert.ok(dropdown.native.bounds.height>open.native.bounds.height+80);
    assert.equal(dropdown.native.bounds.y, open.native.bounds.y, 'Opening the picker grows downward from the same upper edge');
    const evidence=path.join(__dirname,'evidence',require('../package.json').version);await fs.mkdir(evidence,{recursive:true});
    const png=await app.evaluate(async({BrowserWindow})=>(await BrowserWindow.getAllWindows()[0].webContents.capturePage()).toPNG().toString('base64'));
    await fs.writeFile(path.join(evidence,'native-dropdown.png'),Buffer.from(png,'base64'));
    await picker.press('Escape');const dismissed=await fitted();assert.ok(Math.abs(dismissed.native.bounds.height-open.native.bounds.height)<3);
    assert.equal(await page.getByLabel('Your name').isVisible(),true);
    console.log('DROPDOWN',JSON.stringify(dropdown));
    await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(2));
    await page.locator('.install-details summary').click();
    console.log('ZOOM',JSON.stringify(await fitted()));
    await picker.click();await fitted();
    const last=page.getByRole('option',{name:'FL Studio 24',exact:true});await last.scrollIntoViewIfNeeded();
    const lastBox=await last.boundingBox(),height=await page.evaluate(()=>innerHeight);assert.ok(lastBox.y>=0 && lastBox.y+lastBox.height<=height+1);
    await picker.press('Escape');
    await page.getByRole('button',{name:'Create room & open FL Studio',exact:true}).scrollIntoViewIfNeeded();
    await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(1));
    await page.getByRole('button',{name:'Collapse room controls',exact:true}).click();
    const collapsed=await fitted(); assert.ok(collapsed.native.bounds.height<open.native.bounds.height-100);
    console.log('PASS compact window and dropdown grow, shrink and scale without clipping');
  } finally {await app.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
