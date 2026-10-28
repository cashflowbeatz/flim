const assert = require('node:assert/strict');

module.exports = async ({browser, url, check, fixture, open, shot}) => {
  const recovery = await browser.newPage({viewport: {width: 400, height: 900}});
  try {
    await fixture(recovery, {restoreBusy: true}); await open(recovery, url);
    await check('Reloaded controls recover a main-process operation even before any room exists', async () => {
      assert.equal(await recovery.locator('.submit').isDisabled(), true);
      await recovery.evaluate(() => {window.testState.busy = false; window.testState.name = 'Recovered producer'; window.testState.selectedInstallId = 'fl26';});
      await recovery.waitForFunction(() => !document.querySelector('.submit').disabled);
      assert.equal(await recovery.getByLabel('Your name').inputValue(), 'Recovered producer');
      assert.equal(await recovery.getByRole('combobox').evaluate(el => el.value), 'fl26');
      assert.equal(await recovery.evaluate(() => window.testCalls.includes('launch')), false);
    });
    await check('Mouse-highlighted FL choices become the starting point for keyboard navigation', async () => {
      const picker = recovery.getByRole('combobox'); await picker.click();
      await recovery.getByRole('option', {name: 'FL Studio 25', exact: true}).hover();
      assert.equal(await picker.getAttribute('aria-activedescendant'), await recovery.getByRole('option', {name: 'FL Studio 25', exact: true}).getAttribute('id'));
      await picker.press('ArrowDown'); await picker.press('Enter');
      assert.equal(await picker.evaluate(el => el.value), 'fl24');
      await shot(recovery, 'picker-mouse-keyboard-0222');
    });
  } finally {await recovery.close();}
  const room = await browser.newPage({viewport: {width: 400, height: 900}, reducedMotion: 'reduce'});
  try {
    await fixture(room, {restoreRoom: true}); await room.goto(url); await room.getByText('Studio friend', {exact: true}).waitFor();
    await check('Returning focus to room controls immediately checks people without relaunching FL', async () => {
      const before = await room.evaluate(() => window.testCalls.filter(call => call === 'refresh').length);
      await room.evaluate(() => window.dispatchEvent(new Event('focus')));
      await room.waitForFunction(before => window.testCalls.filter(call => call === 'refresh').length > before, before);
      assert.equal(await room.evaluate(() => window.testCalls.includes('launch')), false);
    });
  } finally {await room.close();}
};
