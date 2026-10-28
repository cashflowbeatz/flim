const assert = require('node:assert/strict');

module.exports = async ({browser, url, check, fixture, open, shot}) => {
  async function visit(options, task) {
    const page = await browser.newPage({viewport: {width: 375, height: 800}, reducedMotion: 'reduce'});
    try {await fixture(page, options); await open(page, url); await task(page);}
    finally {await page.close();}
  }
  await visit({}, async page => {
    await check('No Menu, outer card border or circular avatars remain; room actions use a soft switch', async () => {
      assert.equal(await page.getByText('Menu', {exact: true}).count(), 0);
      assert.equal(await page.locator('.room-card').evaluate(el => getComputedStyle(el).borderRadius), '16px');
      assert.equal(await page.getByRole('tablist').evaluate(el => getComputedStyle(el).backgroundColor), 'rgb(233, 236, 240)');
      assert.equal(await page.getByRole('tab', {name: 'Create a room'}).evaluate(el => getComputedStyle(el).borderTopWidth), '0px');
    });
    await check('Room tabs use one keyboard stop and a labelled panel; repeating the current choice preserves errors', async () => {
      const create = page.getByRole('tab', {name: 'Create a room'}), join = page.getByRole('tab', {name: 'Join a room'});
      assert.equal(await create.getAttribute('tabindex'), '0'); assert.equal(await join.getAttribute('tabindex'), '-1');
      assert.equal(await page.getByRole('tabpanel').getAttribute('aria-labelledby'), 'create-tab');
      await page.getByRole('button', {name: 'Create room & open FL Studio'}).click();
      await create.click(); assert.equal(await page.locator('#name-error').count(), 1);
      await create.focus(); await page.keyboard.press('Tab');
      assert.equal(await page.getByLabel('Your name').evaluate(el => el === document.activeElement), true);
    });
    await check('Invalid fields have a dashed blue edge and keyboard focus has no dark perimeter', async () => {
      const input = page.getByLabel('Your name'); await input.focus(); await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
      const style = await input.evaluate(el => {const s=getComputedStyle(el);return {edge:s.borderBottomColor, invalid:s.borderBottomStyle, focus:s.outlineColor, width:s.outlineWidth};});
      assert.equal(style.edge, 'rgb(54, 100, 156)'); assert.equal(style.invalid, 'dashed');
      assert.equal(style.width, '0px');
    });
    await check('Typing immediately after switching mode is not stolen by delayed autofocus', async () => {
      for(let i=0;i<8;i++) {
        await page.getByRole('tab', {name: 'Join a room'}).click();
        await page.getByLabel('Room number').fill('1111 2222');
        await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
        assert.equal(await page.getByLabel('Room number').inputValue(), '1111 2222');
        await page.getByRole('tab', {name: 'Create a room'}).click();
      }
    });
    await page.getByLabel('Your name').fill('Producer');
    await page.getByRole('tab', {name: 'Join a room'}).click();
    await check('Ordinary paste accepts international digits and separators, and rejects unrelated clipboard text', async () => {
      const input = page.getByLabel('Room number');
      const paste = text => input.evaluate((el, text) => {const data=new DataTransfer();data.setData('text/plain',text);el.dispatchEvent(new ClipboardEvent('paste',{clipboardData:data,bubbles:true,cancelable:true}));}, text);
      for(const code of ['１２３４ ５６７８','١٢٣٤–٥٦٧٨','\n1234\u00a05678\t']) {await paste(code);assert.equal(await input.inputValue(),'1234 5678');}
      await paste('the room is 12345678');
      assert.equal(await input.inputValue(),'1234 5678'); assert.match(await page.locator('#code-error').textContent(), /just the eight-digit/);
      assert.equal((await page.evaluate(() => window.testCalls)).length,0);
    });
    await check('Room numbers format on blur while middle edits retain the caret', async () => {
      const input=page.getByLabel('Room number');await input.fill('12345678');await page.getByLabel('Your name').click();assert.equal(await input.inputValue(),'1234 5678');
      await input.focus();await input.press('Home');await input.press('ArrowRight');await input.press('Delete');
      assert.equal(await input.inputValue(),'134 5678');assert.equal(await input.evaluate(el=>el.selectionStart),1);
    });
    await check('Reopening Join restores the next useful field and canceled pending paste cannot overwrite the draft', async () => {
      await page.evaluate(()=>{window.testDeferPaste=true;});
      await page.getByRole('button',{name:'Paste room number'}).click();await page.waitForFunction(()=>Boolean(window.testReleasePaste));
      await page.getByRole('button',{name:'Collapse room controls'}).click();await page.evaluate(()=>window.testReleasePaste());
      await page.getByRole('button',{name:'Get started',exact:true}).click();
      await page.waitForFunction(()=>document.activeElement?.id==='room-code');
      assert.equal(await page.getByLabel('Room number').inputValue(),'134 5678');
    });
    await check('Returning to setup resets Join and its old validation without losing the name or FL choice', async () => {
      await page.getByLabel('Room number').fill('1234 5678');
      await page.getByRole('button',{name:'Join room & open FL Studio'}).click();await page.getByText('Studio friend',{exact:true}).waitFor();
      assert.equal(await page.locator('.person-initial').count(),0);
      assert.equal(await page.locator('.people li').first().evaluate(el=>getComputedStyle(el).borderBottomWidth),'0px');
      assert.equal(await page.getByRole('button',{name:'Copy room number'}).evaluate(el=>getComputedStyle(el).borderTopWidth),'0px');
      await page.getByRole('button',{name:'Return to start'}).click();
      assert.equal(await page.getByRole('tab',{name:'Create a room'}).getAttribute('aria-selected'),'true');
      assert.equal(await page.getByLabel('Your name').inputValue(),'Producer');
      await page.getByRole('tab',{name:'Join a room'}).click();assert.equal(await page.getByLabel('Room number').inputValue(),'');assert.equal(await page.locator('#code-error').count(),0);
    });
    await check('Logs open directly without a menu and rapid clicks cannot launch duplicate requests', async () => {
      await page.evaluate(()=>{window.testDeferLogs=true;});
      await page.getByRole('button',{name:'Open test logs',exact:true}).evaluate(el=>{el.click();el.click();});
      await page.waitForFunction(()=>Boolean(window.testReleaseLogs));
      assert.equal((await page.evaluate(()=>window.testCalls)).filter(call=>call==='logs').length,1);
      await page.evaluate(()=>window.testReleaseLogs());await page.getByRole('button',{name:'Open test logs',exact:true}).waitFor();
    });
    await shot(page,'clean-join');
  });
  for(const busy of [false,true]) {
    const page=await browser.newPage();
    try {
      await fixture(page,{restoreRoom:true,restoreBusy:busy,waiting:busy});await page.goto(url);
      await check(busy?'Reloaded controls respect a native action still in progress':'Reloaded controls open the existing room and immediately refresh its people',async()=>{
        await page.getByRole('heading',{name:'Room 1234 5678',exact:true}).waitFor();
        if(busy)assert.equal(await page.getByRole('button',{name:'Finishing the current step…'}).isDisabled(),true);
        else await page.getByText('Studio friend',{exact:true}).waitFor();
        assert.equal((await page.evaluate(()=>window.testCalls)).filter(call=>call==='host'||call==='member'||call==='launch').length,0);
        if(busy)assert.equal(await page.getByRole('button',{name:'Return to start'}).isDisabled(),true);
      });
    }finally{await page.close();}
  }
};
