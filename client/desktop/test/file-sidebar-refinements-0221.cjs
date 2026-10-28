const assert = require('node:assert/strict');
const path = require('node:path');
module.exports = async ({visit, check, evidence}) => {
  const open = page => page.getByRole('button', {name: 'Files', exact: true}).click();
  await visit(680, async page => {
    await page.evaluate(() => window.failFit = true); await open(page);
    await check('Unavailable window fitting is logged without breaking folder or file controls', async () => {
      await page.waitForFunction(() => window.fileCalls.some(call => call.error?.message.includes('Window fitting failed')));
      await page.getByRole('searchbox').fill('kick'); await page.getByText('Showing 1 of 4 files', {exact: true}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Open samples folder', exact: true}).isEnabled(), true);
    });
  });
  await visit(680, async page => {
    await open(page); const choose = page.getByRole('button', {name: 'Choose folder', exact: true});
    await check('Received-file location can be chosen before joining and saves visibly without a menu', async () => {
      await page.getByText('D:\\Music\\Received samples', {exact: true}).waitFor(); await choose.click();
      await page.getByText('D:\\Audio\\New received samples', {exact: true}).waitFor();
      await page.getByText('Folder saved for your next FL session.', {exact: false}).waitFor();
      assert.equal(await page.evaluate(() => window.fileCalls.filter(call => call === 'get').length), 0);
    });
    await check('Canceled folder selection keeps the saved path and does not announce a new save', async () => {
      await page.evaluate(() => window.cancelLocation = true); await choose.click();
      await page.getByText('D:\\Audio\\New received samples', {exact: true}).waitFor();
      assert.equal(await page.getByText('Folder saved for your next FL session.', {exact: false}).count(), 0);
    });
    await check('Folder persistence failures are visible and the previous path stays selected', async () => {
      await page.evaluate(() => {window.cancelLocation = false; window.locationFailure = true;}); await choose.click();
      await page.getByRole('alert').filter({hasText: 'could not be saved'}).waitFor();
      assert.equal(await page.getByText('D:\\Audio\\New received samples', {exact: true}).isVisible(), true);
      await page.evaluate(() => window.locationFailure = false);
    });
    await check('Use default restores Documents, removes the extra reset action and keeps Files open', async () => {
      await page.getByRole('button', {name: 'Use default', exact: true}).click();
      await page.getByText('C:\\Users\\Producer\\Documents\\flim\\samples', {exact: true}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Use default', exact: true}).count(), 0);
      assert.equal(await page.getByRole('button', {name: 'Hide files', exact: true}).isVisible(), true);
      await page.screenshot({path: path.join(evidence, 'folder-before-room.png'), fullPage: true});
    });
    await check('Repeated clicks share a pending picker and a hidden panel ignores its late reply', async () => {
      await page.evaluate(() => window.deferLocation = true); await choose.click();
      const choosing = page.getByRole('button', {name: 'Choosing folder…', exact: true});
      await page.waitForFunction(() => Boolean(window.releaseLocation)); assert.equal(await choosing.isDisabled(), true);
      const count = await page.evaluate(() => window.fileCalls.filter(call => call === 'choose-folder').length);
      await choosing.evaluate(button => button.click()); assert.equal(await page.evaluate(() => window.fileCalls.filter(call => call === 'choose-folder').length), count);
      await page.getByRole('button', {name: 'Hide files', exact: true}).click(); await page.evaluate(() => window.releaseLocation());
      assert.equal(await page.locator('.sample-location').count(), 0);
    });
  }, {}, {noRoom: true});
  await visit(680, async page => {
    await open(page);
    await check('Active rooms explain the folder lock and keep the open-folder action available', async () => {
      await page.getByText('D:\\Music\\Received samples', {exact: true}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Choose folder', exact: true}).isDisabled(), true);
      assert.equal(await page.getByRole('button', {name: 'Use default', exact: true}).isDisabled(), true);
      assert.equal(await page.getByRole('button', {name: 'Open samples folder', exact: true}).isEnabled(), true);
      await page.getByText('Running FL sessions keep their current location.', {exact: false}).waitFor();
    });
    await check('Closing a room preserves confirmed files without another automatic read', async () => {
      await page.getByText('kick.wav', {exact: true}).waitFor(); await page.clock.install();
      const reads = await page.evaluate(() => window.fileCalls.filter(call => call === 'get').length);
      await page.evaluate(() => window.fileRoomState.room.status = 'ended');
      await page.getByRole('button', {name: 'Refresh people', exact: true}).click();
      await page.getByText('This room has closed.', {exact: false}).waitFor();
      assert.equal(await page.evaluate(() => window.fileCalls.filter(call => call === 'get').length), reads);
      await page.clock.fastForward(20000); assert.equal(await page.evaluate(() => window.fileCalls.filter(call => call === 'get').length), reads);
      assert.equal(await page.getByText('kick.wav', {exact: true}).isVisible(), true);
    });
  });
  await visit(400, async page => {
    await page.evaluate(() => window.locationReadFailure = true); await open(page);
    await check('Folder-load errors have a retry action and recovery does not disturb room setup', async () => {
      await page.getByRole('alert').filter({hasText: 'saved folder could not be checked'}).waitFor();
      await page.evaluate(() => window.locationReadFailure = false); await page.getByRole('button', {name: 'Retry folder check'}).click();
      await page.getByText('D:\\Music\\Received samples', {exact: true}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Choose folder'}).isEnabled(), true);
    });
  }, {}, {noRoom: true});
  await visit(400, async page => {
    await page.evaluate(() => {window.sampleLocation.custom = false; window.sampleLocation.error = 'The saved samples folder cannot be used.';}); await open(page);
    await check('Invalid saved folders offer Use default even when no valid custom path was recovered', async () => {
      await page.getByRole('alert').filter({hasText: 'cannot be used'}).waitFor();
      assert.equal(await page.getByRole('button', {name:'Open samples folder', exact:true}).isDisabled(), true);
      await page.evaluate(() => window.sampleLocation.error = '');
      await page.getByRole('button', {name:'Use default', exact:true}).click();
      await page.getByText('Default samples folder', {exact:true}).waitFor();
      assert.equal(await page.getByRole('alert').count(), 0);
    });
  }, {}, {noRoom: true});
  for (const width of [280, 400, 599, 680, 1200]) await visit(width, async page => {
    await page.evaluate(() => window.sampleLocation.path = 'D:\\Very long shared music library\\'.repeat(5)); await open(page);
    await page.getByText('Your chosen folder', {exact: true}).waitFor();
    await check(`Destination text wraps and folder controls remain keyboard accessible at ${width}px`, async () => {
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      const choose = page.getByRole('button', {name: 'Choose folder', exact: true}); await choose.focus(); await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
      assert.equal(await choose.evaluate(el => el === document.activeElement), true);
      assert.ok((await choose.boundingBox()).height >= 44);
      assert.equal(await page.locator('.sample-location-path').getAttribute('dir'), 'ltr');
      await page.screenshot({path: path.join(evidence, `folder-${width}.png`), fullPage: true});
    });
  }, {}, {noRoom: true});
  await visit(680, async page => {
    await page.evaluate(() => window.fileItems = [{id:'storage', name:'kick.wav', direction:'download', state:'waiting', reason:'local_cache', bytes:10000, done:0, updatedAt:1}]); await open(page);
    await check('Local disk failures explain folder recovery and retain pending edit status', async () => {
      await page.getByText('Waiting for folder access', {exact:true}).waitFor();
      await page.getByText('Check the received-files folder, drive access and free space.', {exact:false}).waitFor();
      assert.equal(await page.getByRole('progressbar').count(), 0);
    });
  });
};
