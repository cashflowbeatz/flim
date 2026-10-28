const assert = require('node:assert/strict');
const path = require('node:path');

module.exports = async ({visit, check, evidence}) => {
  const toggle = page => page.getByRole('button', {name: 'Files', exact: true}).click();
  const count = page => page.evaluate(() => window.fileCalls.filter(call => call === 'get').length);
  const refresh = page => page.getByRole('button', {name: 'Refresh files', exact: true}).click();
  await visit(680, async page => {
    await page.evaluate(() => window.fileItems.push(...[10, 2].map(n => ({id: String(n), name: `Kíck ${n}.wav`, direction: 'upload', state: 'ready', reason: '', bytes: 100, done: 100, updatedAt: n}))));
    await toggle(page);
    await page.getByText('Showing 6 of 6 files', {exact: true}).waitFor();
    await check('File search ignores accents and case and uses natural filename order', async () => {
      await page.getByRole('searchbox', {name: 'Find a file'}).fill('KICK');
      assert.deepEqual(await page.locator('.file-name').allTextContents(), ['kick.wav', 'Kíck 2.wav', 'Kíck 10.wav']);
      await page.getByText('Showing 3 of 6 files', {exact: true}).waitFor();
    });
    await check('Status filters combine with search and support arrows, Home and End', async () => {
      const toolbar = page.getByRole('toolbar', {name: 'File status filter'});
      await toolbar.getByRole('button', {name: 'All', exact: true}).focus(); await page.keyboard.press('End');
      assert.equal(await toolbar.getByRole('button', {name: 'Ready', exact: true}).getAttribute('aria-pressed'), 'true');
      assert.deepEqual(await page.locator('.file-name').allTextContents(), ['Kíck 2.wav', 'Kíck 10.wav']);
      await page.keyboard.press('ArrowLeft');
      await page.getByText('No files match this search or filter.', {exact: true}).waitFor();
      await page.getByRole('button', {name: 'Show all files', exact: true}).click();
      assert.equal(await page.getByRole('searchbox').inputValue(), '');
      assert.equal(await page.getByRole('searchbox').evaluate(el => el === document.activeElement), true);
      assert.equal(await toolbar.locator('button[tabindex="0"]').count(), 1);
      await toolbar.getByRole('button', {name: 'All', exact: true}).focus(); await page.keyboard.press('ArrowDown');
      assert.equal(await toolbar.getByRole('button', {name: 'Attention', exact: true}).getAttribute('aria-pressed'), 'true');
      await page.keyboard.press('Home');
      await toolbar.getByRole('button', {name: 'Pending', exact: true}).click();
      assert.deepEqual(await page.locator('.file-name').allTextContents(), ['kick.wav']);
      await toolbar.getByRole('button', {name: 'All', exact: true}).click();
    });
    await check('File search has a stable clear action and Escape clears before closing', async () => {
      const query = page.getByRole('searchbox');
      await query.fill('snare'); await page.getByRole('button', {name: 'Clear file search'}).click();
      assert.equal(await query.inputValue(), ''); assert.equal(await query.evaluate(el => el === document.activeElement), true);
      const style = await query.evaluate(el => {const s = getComputedStyle(el); return [s.outlineWidth, s.borderTopWidth, s.borderLeftWidth, s.borderRightWidth, s.borderBottomColor];});
      assert.deepEqual(style, ['0px', '0px', '0px', '0px', 'rgb(54, 100, 156)']);
      await query.fill('kick'); await page.keyboard.press('Escape');
      assert.equal(await query.inputValue(), ''); assert.equal(await page.locator('.files-sidebar').count(), 1);
      await page.getByRole('button', {name: 'Hide files', exact: true}).focus(); await page.keyboard.press('Control+f');
      assert.equal(await query.evaluate(el => el === document.activeElement), true);
      await page.keyboard.press('Escape'); assert.equal(await page.locator('.files-sidebar').count(), 0);
    });
    await toggle(page); await page.getByText('Showing 6 of 6 files', {exact: true}).waitFor();
    await page.screenshot({path: path.join(evidence, 'search-and-filters.png'), fullPage: true});
  });
  await visit(400, async page => {
    await page.evaluate(() => window.fileItems.push({id: 'wait', name: 'pending.wav', direction: 'upload', state: 'waiting', reason: 'retrying', bytes: 0, done: 0, updatedAt: 1}));
    await toggle(page);
    await check('Waiting is separate from active work and progress includes direction, bytes and accessible text', async () => {
      await page.getByText('1 transferring · 1 waiting · 1 ready · 2 need attention', {exact: true}).waitFor();
      assert.equal(await page.getByText('Size not known yet', {exact: true}).isVisible(), true);
      const progress = page.getByRole('progressbar', {name: 'kick.wav: Downloading'});
      assert.equal(await progress.getAttribute('aria-valuetext'), '48.8 KB of 97.7 KB · 50%');
      assert.equal(await page.getByText('From this computer', {exact: true}).count(), 1);
      assert.equal(await page.getByText('To this computer', {exact: true}).count(), 4);
    });
  });
  await visit(400, async page => {
    await page.clock.install();
    await page.clock.pauseAt(new Date(Date.now() + 1000));
    await page.evaluate(() => {window.fileItems = []; window.fileUnavailable = true;});
    await toggle(page);
    await check('An unavailable first read explains missing confirmation instead of claiming an empty room', async () => {
      await page.getByText('No confirmed file list is available yet.', {exact: false}).waitFor();
      assert.equal(await page.getByText('Add a sample to a shared channel or playlist', {exact: false}).count(), 0);
    });
    await check('Status failures back off for 2, 4, 8 and 15 seconds then return to the normal interval', async () => {
      let before = await count(page);
      for (const delay of [2000, 4000, 8000, 15000]) {
        await page.clock.fastForward(delay - 1); assert.equal(await count(page), before);
        await page.clock.fastForward(1); await page.waitForFunction(n => window.fileCalls.filter(call => call === 'get').length === n, ++before);
        await page.waitForFunction(() => !document.querySelector('.files-refresh').disabled);
      }
      await page.evaluate(() => window.fileUnavailable = false); await refresh(page);
      await page.waitForFunction(() => !document.querySelector('.files-refresh').disabled); before = await count(page);
      await page.clock.fastForward(1000); await page.waitForFunction(n => window.fileCalls.filter(call => call === 'get').length === n, before + 1);
    });
  });
  await visit(680, async page => {
    await page.clock.install(); await toggle(page); await page.getByText('Showing 4 of 4 files', {exact: true}).waitFor();
    await check('Manual refresh shares one in-flight read with scheduled checks and visibly completes', async () => {
      const before = await count(page); await page.evaluate(() => window.deferFiles = true); await refresh(page);
      await page.waitForFunction(() => Boolean(window.releaseFiles));
      assert.equal(await page.getByRole('button', {name: 'Checking files…', exact: true}).isDisabled(), true);
      await page.clock.fastForward(5000); assert.equal(await count(page), before + 1);
      await page.evaluate(() => {window.deferFiles = false; window.releaseFiles();});
      await page.getByRole('button', {name: 'Refresh files', exact: true}).waitFor();
    });
    await check('Hidden file views stop scheduled reads and refresh when visible again', async () => {
      const before = await count(page);
      await page.evaluate(() => {Object.defineProperty(document, 'hidden', {value: true, configurable: true}); document.dispatchEvent(new Event('visibilitychange'));});
      await page.clock.fastForward(30000); assert.equal(await count(page), before);
      await page.evaluate(() => {Object.defineProperty(document, 'hidden', {value: false, configurable: true}); document.dispatchEvent(new Event('visibilitychange'));});
      await page.waitForFunction(n => window.fileCalls.filter(call => call === 'get').length === n, before + 1);
    });
    await check('Automatic checks keep the refresh label still and a manual click joins a pending automatic read', async () => {
      await page.evaluate(() => {window.deferFiles = true; window.releaseFiles = null;});
      const before = await count(page); await page.clock.fastForward(1001);
      await page.waitForFunction(() => Boolean(window.releaseFiles));
      assert.equal(await page.getByRole('button', {name: 'Refresh files', exact: true}).isEnabled(), true);
      await refresh(page);
      assert.equal(await page.getByRole('button', {name: 'Checking files…', exact: true}).isDisabled(), true);
      assert.equal(await count(page), before + 1);
      await page.evaluate(() => {window.deferFiles = false; window.releaseFiles();});
      await page.getByRole('button', {name: 'Refresh files', exact: true}).waitFor();
    });
    await check('A stale flag preserves confirmed files even when available is still true', async () => {
      await page.evaluate(() => {window.fileItems = []; window.fileStale = true;}); await refresh(page);
      await page.getByText('The last confirmed list is shown.', {exact: false}).waitFor();
      assert.equal(await page.locator('.file-name').count(), 4);
    });
    await check('A room closing during a failed read retains its confirmed list and current search', async () => {
      await page.getByRole('searchbox').fill('kick');
      await page.evaluate(() => {window.fileRoomState.room.status = 'ended'; window.fileFailures = true;});
      await page.getByRole('button', {name: 'Refresh people', exact: true}).click();
      await page.getByText('This room has closed.', {exact: false}).waitFor();
      assert.equal(await page.getByRole('searchbox').inputValue(), 'kick');
      assert.deepEqual(await page.locator('.file-name').allTextContents(), ['kick.wav']);
    });
  });
  await visit(400, async page => {
    await toggle(page);
    await check('Older controls without a file-status API show a recovery instruction without polling', async () => {
      await page.getByText('Reopen the complete current flim app', {exact: false}).waitFor();
      assert.equal(await count(page), 0); assert.equal(await page.getByRole('searchbox').count(), 0);
    });
  }, {}, {noFilesApi: true});
  await visit(680, async page => {
    await page.clock.install(); await toggle(page); await page.getByText('Showing 4 of 4 files', {exact: true}).waitFor();
    await check('Closed rooms show their last activity and stop automatic file-status checks', async () => {
      await page.getByText('This room has closed.', {exact: false}).waitFor();
      const before = await count(page); await page.clock.fastForward(60000); assert.equal(await count(page), before);
      assert.equal(await page.locator('.file-name').count(), 4);
    });
  }, {}, {ended: true});
  await visit(280, async page => {
    await toggle(page); await page.getByText('Showing 4 of 4 files', {exact: true}).waitFor();
    await check('File filters retain visible keyboard selection and large targets in forced colors', async () => {
      const toolbar = page.getByRole('toolbar', {name: 'File status filter'});
      await toolbar.getByRole('button', {name: 'All', exact: true}).focus(); await page.keyboard.press('ArrowRight');
      const pending = toolbar.getByRole('button', {name: 'Pending', exact: true});
      assert.equal(await pending.evaluate(el => el === document.activeElement), true);
      assert.equal(await pending.getAttribute('aria-pressed'), 'true');
      assert.equal(await pending.evaluate(el => getComputedStyle(el).outlineStyle), 'solid');
      for (const button of await toolbar.getByRole('button').all()) {
        const box = await button.boundingBox(); assert.ok(box.width >= 44 && box.height >= 44);
      }
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await page.screenshot({path: path.join(evidence, 'filters-forced-colors.png'), fullPage: true});
    });
  }, {forcedColors: 'active'});
};
