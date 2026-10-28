const assert = require('node:assert/strict');
const path = require('node:path');

module.exports = async ({visit, check, evidence}) => {
  const open = page => page.getByRole('button', {name: 'Files', exact: true}).click();
  await visit(680, async page => {
    await check('Find shortcut opens the Files sidebar directly from room controls and focuses its search', async () => {
      await page.getByRole('button', {name: 'Copy room number', exact: true}).focus(); await page.keyboard.press('Control+f');
      await page.getByLabel('Find a file').waitFor();
      assert.equal(await page.getByLabel('Find a file').evaluate(el => el === document.activeElement), true);
      await page.getByLabel('Find a file').fill('kick'); await page.keyboard.press('Control+f');
      assert.equal(await page.getByLabel('Find a file').evaluate(el => el.selectionEnd - el.selectionStart), 4);
    });
    await check('Modified find shortcuts are left available and do not steal focus', async () => {
      const target = page.getByRole('button', {name: 'To me', exact: true}); await target.focus();
      await target.dispatchEvent('keydown', {key: 'f', ctrlKey: true, shiftKey: true});
      assert.equal(await target.evaluate(el => el === document.activeElement), true);
    });
    await check('Upload and download filters combine with search and status counts without mixing directions', async () => {
      await page.getByLabel('Find a file').fill('');
      await page.evaluate(() => {window.fileItems.push({id: 'up', name: 'sent bass.wav', direction: 'upload', state: 'uploading', reason: '', bytes: 1000, done: 500, updatedAt: Date.now()});});
      await page.getByRole('button', {name: 'Refresh files', exact: true}).click(); await page.getByText('sent bass.wav', {exact: true}).waitFor();
      await page.getByRole('button', {name: 'From me', exact: true}).click();
      assert.deepEqual(await page.locator('.file-name').allTextContents(), ['sent bass.wav']);
      assert.equal(await page.getByRole('button', {name: 'Pending', exact: true}).getAttribute('aria-describedby').then(id => page.locator('#' + id).textContent()), '1 matching files');
      await page.getByRole('button', {name: 'To me', exact: true}).click();
      assert.equal(await page.getByText('sent bass.wav', {exact: true}).count(), 0); assert.equal(await page.locator('.file-name').count(), 4);
    });
    await check('Direction choices survive hiding Files and Show all clears direction along with search', async () => {
      await page.getByRole('button', {name: 'From me', exact: true}).click(); await page.getByRole('button', {name: 'Hide files', exact: true}).click(); await open(page);
      assert.equal(await page.getByRole('button', {name: 'From me', exact: true}).getAttribute('aria-pressed'), 'true');
      await page.getByLabel('Find a file').fill('not-present'); await page.getByRole('button', {name: 'Show all files', exact: true}).click();
      assert.equal(await page.getByRole('button', {name: 'Both', exact: true}).getAttribute('aria-pressed'), 'true');
      assert.equal(await page.getByLabel('Find a file').inputValue(), ''); await page.getByText('sent bass.wav', {exact: true}).waitFor();
    });
    await check('Capped activity lists clearly explain that other transfers still continue', async () => {
      await page.evaluate(() => {window.fileTotal = 140; window.fileTruncated = true;});
      await page.getByRole('button', {name: 'Refresh files', exact: true}).click(); await page.getByText('from at least 140 files', {exact: false}).waitFor();
      await page.screenshot({path: path.join(evidence, 'direction-and-limit-0222.png'), fullPage: true});
    });
  });

  await visit(400, async page => {
    await open(page);
    await check('Choosing and resetting a folder show their actual pending action and restore a useful focus target', async () => {
      await page.evaluate(() => {window.deferLocation = true;});
      await page.getByRole('button', {name: 'Choose folder', exact: true}).click();
      assert.equal(await page.getByRole('button', {name: 'Choosing folder…', exact: true}).isDisabled(), true);
      await page.getByRole('status').filter({hasText: 'Choosing folder…'}).waitFor();
      await page.evaluate(() => {window.releaseLocation(); window.deferLocation = false;});
      await page.getByRole('button', {name: 'Choose folder', exact: true}).waitFor();
      await page.getByRole('button', {name: 'Use default', exact: true}).click();
      await page.waitForFunction(() => ![...document.querySelectorAll('button')].some(el => el.textContent === 'Use default'));
      assert.equal(await page.getByRole('button', {name: 'Choose folder', exact: true}).evaluate(el => el === document.activeElement), true);
    });
  }, {}, {noRoom: true});

  await visit(400, async page => {
    await open(page); await page.getByText('Your chosen folder', {exact: true}).waitFor();
    await check('Folder permissions are refreshed when a recovered background operation unlocks setup', async () => {
      assert.equal(await page.getByRole('button', {name: 'Choose folder', exact: true}).isDisabled(), true);
      await page.evaluate(() => {window.fileRoomState.busy = false;});
      await page.waitForFunction(() => ![...document.querySelectorAll('button')].find(el => el.textContent === 'Choose folder').disabled);
    });
  }, {}, {noRoom: true, busy: true});

  await visit(680, async page => {
    await page.evaluate(() => {delete window.flimDesktop.chooseSampleLocation; delete window.flimDesktop.resetSampleLocation;}); await open(page);
    await check('Partial folder bridges disable unsupported actions while keeping supported folder opening usable', async () => {
      assert.equal(await page.getByRole('button', {name: 'Choose folder', exact: true}).isDisabled(), true);
      assert.equal(await page.getByRole('button', {name: 'Use default', exact: true}).isDisabled(), true);
      assert.equal(await page.getByRole('button', {name: 'Open samples folder', exact: true}).isEnabled(), true);
    });
  }, {}, {noRoom: true});

  await visit(400, async page => {
    await page.evaluate(() => {window.fileItems = Array.from({length: 30}, (_, i) => ({id: String(i), name: `sample-${i}.wav`, direction: 'download', state: 'ready', reason: '', bytes: 10, done: 10, updatedAt: Date.now()}));});
    await open(page);
    await check('The file-list scroll hint identifies the end instead of promising more hidden rows', async () => {
      await page.getByText('Scroll for more files', {exact: true}).waitFor();
      await page.getByRole('region', {name: 'File transfer list', exact: true}).evaluate(el => {el.scrollTop = el.scrollHeight;});
      await page.getByText('End of file list', {exact: true}).waitFor();
      await page.screenshot({path: path.join(evidence, 'list-end-0222.png'), fullPage: true});
    });
  });

  await visit(400, async page => {
    await page.evaluate(() => {window.fileFailures = true; window.fileItems = [];}); await open(page);
    await page.getByText('No confirmed file list is available yet.', {exact: false}).waitFor();
    await page.getByRole('button', {name: 'Hide files', exact: true}).click();
    await page.evaluate(() => {window.fileRoomState.room.status = 'ended'; window.fileFailures = false; window.fileItems = [{id: 'late', name: 'late-confirmed.wav', direction: 'download', state: 'ready', reason: '', bytes: 10, done: 10, updatedAt: Date.now()}];});
    await page.getByRole('button', {name: 'Refresh people', exact: true}).click();
    await open(page);
    await check('A closed room with only a failed empty snapshot can still retrieve its first confirmed file list', async () => {
      await page.getByText('late-confirmed.wav', {exact: true}).waitFor();
      assert.equal(await page.locator('.files-room').textContent(), 'Room 1234 5678 · Closed');
    });
  });

  for (const width of [280, 599, 680]) await visit(width, async page => {
    await open(page);
    await check(`Direction controls fit and remain keyboard-operable at ${width}px`, async () => {
      await page.getByRole('button', {name: 'To me', exact: true}).focus(); await page.keyboard.press('Enter');
      assert.equal(await page.getByRole('button', {name: 'To me', exact: true}).getAttribute('aria-pressed'), 'true');
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      for (const button of await page.locator('.file-directions button').all()) {const box = await button.boundingBox(); assert.ok(box.width >= 44 && box.height >= 44);}
    });
  }, {forcedColors: width === 280 ? 'active' : 'none'});
};
