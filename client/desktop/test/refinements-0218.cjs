const assert = require('node:assert/strict');

module.exports = async ({browser, url, check, fixture, open, shot}) => {
  const page = await browser.newPage({viewport: {width: 400, height: 900}, reducedMotion: 'reduce'});
  try {
    await fixture(page); await open(page, url);
    await page.getByLabel('Your name').fill('Input fixture');
    await page.getByRole('tab', {name: 'Join a room', exact: true}).click();
    await check('A delayed room-number paste cannot overwrite a draft after focus moves elsewhere', async () => {
      await page.getByLabel('Room number').fill('12345678');
      await page.evaluate(() => window.testDeferPaste = true);
      await page.getByRole('button', {name: 'Paste room number', exact: true}).click();
      await page.waitForFunction(() => Boolean(window.testReleasePaste));
      await page.getByLabel('Your name').focus(); await page.evaluate(() => window.testReleasePaste());
      await page.getByRole('button', {name: 'Paste room number', exact: true}).waitFor();
      assert.equal(await page.getByLabel('Room number').inputValue(), '1234 5678');
      assert.equal(await page.getByLabel('Your name').evaluate(el => el === document.activeElement), true);
    });
    await check('Composing text does not submit a room, collapse setup or operate the FL picker', async () => {
      const canceled = await page.getByLabel('Your name').evaluate(el => !el.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', code: 'Enter', isComposing: true, bubbles: true, cancelable: true})));
      assert.equal(canceled, true);
      await page.getByLabel('Your name').dispatchEvent('keydown', {key: 'Escape', isComposing: true});
      assert.equal(await page.getByLabel('Your name').isVisible(), true);
      const picker = page.getByRole('combobox'); await picker.focus();
      await picker.dispatchEvent('keydown', {key: 'Enter', isComposing: true});
      assert.equal(await picker.getAttribute('aria-expanded'), 'false');
      await picker.dispatchEvent('keydown', {key: 'Enter', keyCode: 229});
      assert.equal(await picker.getAttribute('aria-expanded'), 'false');
      assert.equal(await page.evaluate(() => window.testCalls.some(call => call === 'host' || call === 'member')), false);
    });
    await check('FL choice navigation scrolls only its list and keeps the document still', async () => {
      await page.setViewportSize({width: 280, height: 320});
      const picker = page.getByRole('combobox'); await picker.click();
      const before = await page.evaluate(() => scrollY);
      await page.keyboard.press('End'); await page.keyboard.press('Home'); await page.keyboard.press('End');
      assert.equal(await page.evaluate(() => scrollY), before);
      await page.keyboard.press('Enter');
      assert.equal(await picker.evaluate(el => el === document.activeElement), true);
      assert.equal(await page.evaluate(() => scrollY), before);
    });
  } finally {await page.close();}

  for (const width of [320, 400, 680]) {
    const room = await browser.newPage({viewport: {width, height: 900}, reducedMotion: 'reduce'});
    try {
      await fixture(room, {restoreRoom: true}); await room.goto(url);
      await room.getByText('Studio friend', {exact: true}).waitFor();
      await check(`People scrolling follows measured overflow at ${width}px`, async () => {
        await room.waitForFunction(() => !document.querySelector('#people-scroll-help'));
        assert.equal(await room.locator('.people-scroll').getAttribute('tabindex'), null);
        if (width === 320) assert.equal(await room.locator('.people').evaluate(el => getComputedStyle(el).gridTemplateColumns.split(' ').length), 1);
        await room.evaluate(() => window.testPeers = Array.from({length: 8}, (_, i) => ({id: String(i), name: `Participant ${i} with a longer studio name`, role: i ? 'member' : 'host', fl: '26.1.7.5654'})));
        await room.getByRole('button', {name: 'Refresh people', exact: true}).click();
        await room.getByText('Scroll for everyone', {exact: true}).waitFor();
        assert.equal(await room.locator('.people-scroll').getAttribute('tabindex'), '0');
        await shot(room, `people-refined-${width}`);
      });
    } finally {await room.close();}
  }
  for (const width of [320, 680]) {
    const room = await browser.newPage({viewport: {width, height: 900}, reducedMotion: 'reduce'});
    try {
      await fixture(room, {restoreRoom: true, logsError: true}); await room.goto(url);
      await room.getByText('Studio friend', {exact: true}).waitFor();
      await check(`A late diagnostics error stays visible beside Files and dismiss restores focus at ${width}px`, async () => {
        await room.evaluate(() => window.testDeferLogs = true);
        await room.getByRole('button', {name: 'Open test logs', exact: true}).click();
        await room.waitForFunction(() => Boolean(window.testReleaseLogs));
        await room.getByRole('button', {name: 'Files', exact: true}).click();
        await room.evaluate(() => window.testReleaseLogs());
        const feedback = room.getByRole('alert').filter({hasText: 'test logs folder'}); await feedback.waitFor();
        assert.equal(await feedback.isVisible(), true);
        assert.equal(await room.locator('#files-sidebar h2').evaluate(el => el === document.activeElement), true);
        if (width === 680) assert.equal(await feedback.evaluate(el => getComputedStyle(el).gridColumnEnd), '-1');
        await shot(room, `files-feedback-${width}`);
        await room.getByRole('button', {name: 'Dismiss message', exact: true}).click();
        assert.equal(await feedback.count(), 0);
        assert.equal(await room.locator('#files-sidebar h2').evaluate(el => el === document.activeElement), true);
      });
    } finally {await room.close();}
  }
  const busy = await browser.newPage();
  try {
    await fixture(busy, {restoreRoom: true, restoreBusy: true, waiting: true}); await busy.goto(url);
    await check('Restored main-process work is announced as busy without enabling another launch', async () => {
      const action = busy.getByRole('button', {name: 'Finishing the current step…', exact: false});
      await action.waitFor(); assert.equal(await action.isDisabled(), true);
      assert.equal(await busy.locator('#room-menu').getAttribute('aria-busy'), 'true');
    });
  } finally {await busy.close();}
};
