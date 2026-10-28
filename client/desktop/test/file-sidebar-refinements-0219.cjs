const assert = require('node:assert/strict');
const path = require('node:path');

module.exports = async ({visit, check, evidence}) => {
  const toggle = page => page.getByRole('button', {name: 'Files', exact: true}).click();
  const filter = (page, name) => page.getByRole('toolbar', {name: 'File status filter'}).getByRole('button', {name, exact: true});
  await visit(680, async page => {
    await page.evaluate(() => {window.fileItems = Array.from({length: 20}, (_, i) => ({id: String(i), name: `kick ${i}.wav`, state: 'ready', direction: 'download', reason: '', bytes: 100, done: 100, updatedAt: i}));});
    await toggle(page); await page.getByText('Showing 20 of 20 files', {exact: true}).waitFor();
    await check('Sidebar reopening restores search, filter, order, list position and confirmed files while refreshing', async () => {
      await page.getByRole('searchbox').fill('kick'); await filter(page, 'Ready').click();
      await page.getByRole('button', {name: 'Sort by recent activity'}).click();
      const list = page.getByRole('region', {name: 'File transfer list'});
      await list.evaluate(el => {el.scrollTop = 180; el.dispatchEvent(new Event('scroll', {bubbles: true}));});
      await page.getByRole('button', {name: 'Hide files', exact: true}).click();
      await page.evaluate(() => window.deferFiles = true); await toggle(page);
      await page.waitForFunction(() => Boolean(window.releaseFiles));
      assert.equal(await page.getByRole('searchbox').inputValue(), 'kick');
      assert.equal(await filter(page, 'Ready').getAttribute('aria-pressed'), 'true');
      assert.equal(await page.getByRole('button', {name: 'Sort by file name'}).isVisible(), true);
      assert.equal(await list.evaluate(el => el.scrollTop), 180);
      assert.equal(await page.locator('.file-name').count(), 20);
      await page.getByText('Confirming saved file activity.', {exact: false}).waitFor();
      await page.evaluate(() => {window.deferFiles = false; window.releaseFiles();});
      await page.getByText('Showing 20 of 20 files', {exact: true}).waitFor();
      assert.equal((await page.locator('.file-name').allTextContents())[0], 'kick 19.wav');
    });
    await check('Query, filter and ordering changes reset only the file list scroll', async () => {
      const list = page.locator('.files-list');
      await list.evaluate(el => el.scrollTop = 160); await page.getByRole('searchbox').fill('kick 1');
      assert.equal(await list.evaluate(el => el.scrollTop), 0);
      await list.evaluate(el => el.scrollTop = 160); await filter(page, 'All').click();
      assert.equal(await list.evaluate(el => el.scrollTop), 0);
      await list.evaluate(el => el.scrollTop = 160); await page.getByRole('button', {name: 'Sort by file name'}).click();
      assert.equal(await list.evaluate(el => el.scrollTop), 0);
    });
    await check('Find shortcut selects the existing query and filter keys respect composition and modifiers', async () => {
      const query = page.getByRole('searchbox'); await query.focus(); await query.press('Control+f');
      assert.deepEqual(await query.evaluate(el => [el.selectionStart, el.selectionEnd]), [0, 6]);
      const all = filter(page, 'All'); await all.focus();
      for (const extra of [{isComposing: true}, {keyCode: 229}, {ctrlKey: true}, {altKey: true}, {metaKey: true}, {shiftKey: true}]) {
        await all.dispatchEvent('keydown', {key: 'ArrowRight', ...extra}); assert.equal(await all.getAttribute('aria-pressed'), 'true');
      }
      await query.dispatchEvent('keydown', {key: 'Escape', isComposing: true}); assert.equal(await query.inputValue(), 'kick 1');
    });
    await check('Returning to setup removes the previous room file view and cached rows', async () => {
      await page.getByRole('button', {name: 'Hide files', exact: true}).click();
      await page.getByRole('button', {name: 'Return to start', exact: true}).click(); await toggle(page);
      await page.getByText('Create or join a room to share samples.', {exact: true}).waitFor();
      assert.equal(await page.locator('.file-name').count(), 0);
      await page.evaluate(() => {window.fileRoomState.room = {code: '87654321', role: 'host', maxParticipants: 8, state: 'launched', status: 'active', peers: []};});
    });
  });
  for (const width of [280, 400, 680]) await visit(width, async page => {
    await toggle(page); await page.getByText('Showing 4 of 4 files', {exact: true}).waitFor();
    await check(`Files counts, non-color selection, full-width search and ordering fit at ${width}px`, async () => {
      const all = filter(page, 'All');
      assert.equal(await all.getAttribute('aria-describedby'), 'file-count-all');
      assert.equal(await page.locator('#file-count-all').textContent(), '4 matching files');
      assert.equal(await all.locator('svg').evaluate(el => getComputedStyle(el).visibility), 'visible');
      assert.equal(await filter(page, 'Pending').locator('svg').evaluate(el => getComputedStyle(el).visibility), 'hidden');
      await page.getByRole('searchbox').fill('kick');
      assert.equal(await page.locator('#file-count-attention').textContent(), '0 matching files');
      const query = page.getByRole('searchbox'), clear = page.getByRole('button', {name: 'Clear file search'});
      const q = await query.boundingBox(), c = await clear.boundingBox(); assert.ok(c.x >= q.x && c.x + c.width <= q.x + q.width);
      assert.equal(await page.locator('.files-list').getAttribute('tabindex'), null);
      assert.equal(await page.getByText('Scroll for more files', {exact: true}).count(), 0);
      await clear.click();
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      for (const control of await page.locator('.files-sidebar button, .files-sidebar input').all()) {
        if (!await control.isVisible()) continue;
        const box = await control.boundingBox(); assert.ok(box.height >= 44 && box.width >= 44);
      }
      await page.screenshot({path: path.join(evidence, `refined-0219-${width}.png`), fullPage: true});
    });
  });
  await visit(680, async page => {
    await page.evaluate(() => {window.fileItems.push(...Array.from({length: 5}, (_, i) => ({id: `wait${i}`, name: `wait ${i}.wav`, state: 'waiting', direction: 'download', reason: '', bytes: 0, done: 0, updatedAt: i})), ...Array.from({length: 3}, (_, i) => ({id: `block${i}`, name: `block ${i}.wav`, state: 'blocked', direction: 'download', reason: 'allowance', bytes: 0, done: 0, updatedAt: i})));});
    await toggle(page);
    await check('Long file lists have measured keyboard scrolling, group counts and one explanation per group', async () => {
      await page.getByText('Scroll for more files', {exact: true}).waitFor();
      assert.equal(await page.locator('.files-list').getAttribute('tabindex'), '0');
      assert.equal(await page.getByText('The shared edit is pending', {exact: false}).count(), 1);
      assert.equal(await page.getByText('Checks the allowance again', {exact: false}).count(), 1);
      assert.equal(await page.getByRole('region', {name: 'Waiting', exact: true}).locator('h3 span').textContent(), '5');
      await page.getByRole('region', {name: 'File transfer list'}).focus(); await page.keyboard.press('End');
      await page.waitForFunction(() => document.querySelector('.files-list').scrollTop > 0);
    });
  });
  await visit(680, async page => {
    await page.evaluate(() => {window.fileItems = [2, 10, 1].map(i => ({id: String(i), name: `kick ${i}.wav`, state: 'ready', direction: 'download', reason: '', bytes: 100, done: 100, updatedAt: 1}));});
    await toggle(page); await page.getByText('Showing 3 of 3 files', {exact: true}).waitFor();
    await check('Recent ordering uses natural names to resolve identical timestamps without row jitter', async () => {
      await page.getByRole('button', {name: 'Sort by recent activity'}).click();
      assert.deepEqual(await page.locator('.file-name').allTextContents(), ['kick 1.wav', 'kick 2.wav', 'kick 10.wav']);
    });
    await check('Single-file results use singular text', async () => {
      await page.evaluate(() => window.fileItems = window.fileItems.slice(0, 1));
      await page.getByRole('button', {name: 'Refresh files', exact: true}).click();
      await page.getByText('Showing 1 of 1 file', {exact: true}).waitFor();
    });
  });
};
