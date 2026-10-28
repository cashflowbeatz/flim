const assert = require('node:assert/strict');

module.exports = async ({browser, url, check, fixture, open, shot}) => {
  const page = await browser.newPage({viewport: {width: 400, height: 900}, reducedMotion: 'reduce'});
  try {
    await fixture(page); await open(page, url);
    await check('Room tabs ignore composing, modified and already-handled navigation', async () => {
      const tab = page.getByRole('tab', {name: 'Create a room', exact: true});
      for (const extra of [{isComposing: true}, {keyCode: 229}, {ctrlKey: true}, {altKey: true}, {metaKey: true}, {shiftKey: true}]) {
        await tab.dispatchEvent('keydown', {key: 'ArrowRight', ...extra});
        assert.equal(await tab.getAttribute('aria-selected'), 'true');
      }
      await tab.evaluate(el => {el.addEventListener('keydown', event => event.preventDefault(), {once: true}); el.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowRight', bubbles: true, cancelable: true}));});
      assert.equal(await tab.getAttribute('aria-selected'), 'true');
      await tab.press('ArrowRight'); assert.equal(await page.getByRole('tab', {name: 'Join a room', exact: true}).getAttribute('aria-selected'), 'true');
    });
    await check('Escape closes installation details before collapsing the room form', async () => {
      const summary = page.locator('.install-details summary'); await summary.click();
      await summary.press('Escape'); assert.equal(await page.locator('.install-details').getAttribute('open'), null);
      assert.equal(await page.getByLabel('Your name').isVisible(), true);
      assert.equal(await summary.evaluate(el => el === document.activeElement), true);
      await summary.press('Alt+Escape'); assert.equal(await page.getByLabel('Your name').isVisible(), true);
    });
    await check('FL picker supports Alt+Down, Alt+Up and F4 without changing the choice', async () => {
      const picker = page.getByRole('combobox'); await picker.focus();
      await picker.press('Alt+ArrowDown'); assert.equal(await picker.getAttribute('aria-expanded'), 'true');
      await picker.press('Alt+ArrowUp'); assert.equal(await picker.getAttribute('aria-expanded'), 'false');
      await picker.press('F4'); assert.equal(await picker.getAttribute('aria-expanded'), 'true');
      await picker.press('F4'); assert.equal(await picker.getAttribute('aria-expanded'), 'false');
      const intercepted = await picker.evaluate(el => !el.dispatchEvent(new KeyboardEvent('keydown', {key: 'F4', altKey: true, bubbles: true, cancelable: true})));
      assert.equal(intercepted, false); assert.equal(await picker.getAttribute('aria-expanded'), 'false');
      assert.equal(await picker.evaluate(el => el.value), 'fl25');
    });
    await check('Repeated version-prefix keys cycle through families and exact build descriptions stay accessible', async () => {
      const picker = page.getByRole('combobox'); await picker.click();
      assert.match(await picker.getAttribute('aria-description'), /25\.2\.5\.5319/);
      const first = await picker.getAttribute('aria-activedescendant'); await picker.press('2');
      const second = await picker.getAttribute('aria-activedescendant'); assert.notEqual(second, first);
      await picker.press('2'); assert.notEqual(await picker.getAttribute('aria-activedescendant'), second);
      const options = page.getByRole('option');
      assert.deepEqual(await options.allTextContents(), ['FL Studio 26', 'FL Studio 25', 'FL Studio 24']);
      for (const option of await options.all()) assert.match(await option.getAttribute('aria-description'), /Build \d+\.\d+\.\d+\.\d+/);
      await picker.press('Escape');
    });
  } finally {await page.close();}

  for (const options of [{noInstall: true}, {noNative: true}, {restoreBusy: true}]) {
    const setup = await browser.newPage();
    try {
      await fixture(setup, options); await open(setup, url);
      await check(`Disabled room creation explains its cause and cannot be submitted indirectly: ${Object.keys(options)[0]}`, async () => {
        const help = await setup.locator('#submit-help').textContent();
        assert.match(help, options.noInstall ? /Choose an FL Studio/ : options.noNative ? /restore its FL Studio launcher/ : /Finishing the current step/);
        assert.equal(await setup.locator('.submit').isDisabled(), true);
        await setup.getByLabel('Your name').evaluate(el => {el.value = 'Defensive test'; el.closest('form').requestSubmit();});
        assert.equal(await setup.evaluate(() => window.testCalls.some(call => call === 'host' || call === 'member')), false);
      });
    } finally {await setup.close();}
  }
  const room = await browser.newPage({viewport: {width: 400, height: 900}, reducedMotion: 'reduce'});
  try {
    await fixture(room, {restoreRoom: true}); await room.goto(url); await room.getByText('Studio friend', {exact: true}).waitFor();
    await check('Offline room badge, collapsed caption and title agree without a green open-room claim', async () => {
      await room.evaluate(() => {Object.defineProperty(navigator, 'onLine', {value: false, configurable: true}); window.dispatchEvent(new Event('offline'));});
      assert.equal(await room.locator('.room-top .status').textContent(), 'Offline');
      assert.equal(await room.locator('.room-top .status').getAttribute('class'), 'status waiting');
      await room.waitForFunction(() => document.title === 'flim · Offline');
      await room.getByRole('button', {name: 'Collapse room controls'}).click();
      assert.match(await room.locator('.hero-caption').textContent(), /Offline.*last confirmed/);
      await room.getByRole('button', {name: 'Your room', exact: true}).click();
    });
    await check('Failed status reads replace the active badge and recover consistently', async () => {
      await room.evaluate(() => {window.testFailStatus = true; Object.defineProperty(navigator, 'onLine', {value: true, configurable: true}); window.dispatchEvent(new Event('online'));});
      await room.getByRole('button', {name: 'Retry status', exact: true}).waitFor();
      assert.equal(await room.locator('.room-top .status').textContent(), 'Status unavailable');
      await room.waitForFunction(() => document.title === 'flim · Status unavailable');
      await room.evaluate(() => window.testFailStatus = false); await room.getByRole('button', {name: 'Retry status', exact: true}).click();
      await room.getByText('Room is open', {exact: true}).waitFor();
    });
    await check('Copy stays usable during unrelated room work and exposes pending and completed feedback', async () => {
      await room.evaluate(() => window.testDeferCopy = true);
      await room.getByRole('button', {name: 'Copy room number', exact: true}).click();
      await room.waitForFunction(() => Boolean(window.testReleaseCopy));
      assert.equal(await room.locator('.copy-button').getAttribute('aria-busy'), 'true');
      await room.evaluate(() => window.testReleaseCopy());
      await room.getByRole('button', {name: 'Room number copied', exact: true}).waitFor();
      assert.equal(await room.locator('.copy-button svg rect').count(), 0);
      await shot(room, 'room-refined-0219');
    });
  } finally {await room.close();}
  const busy = await browser.newPage();
  try {
    await fixture(busy, {restoreRoom: true, restoreBusy: true, waiting: true}); await busy.goto(url);
    await check('Copying a room invitation does not wait for a restored FL launch operation', async () => {
      await busy.getByRole('button', {name: 'Finishing the current step…', exact: false}).waitFor();
      await busy.getByRole('button', {name: 'Copy room number', exact: true}).click();
      await busy.getByRole('button', {name: 'Room number copied', exact: true}).waitFor();
      assert.deepEqual(await busy.evaluate(() => window.testCalls), ['copy']);
    });
  } finally {await busy.close();}
  const ended = await browser.newPage();
  try {
    await fixture(ended, {restoreRoom: true, ended: true}); await ended.goto(url);
    await ended.getByText('Room closed', {exact: true}).waitFor();
    await check('A confirmed closed room remains closed when the computer later goes offline', async () => {
      await ended.evaluate(() => {Object.defineProperty(navigator, 'onLine', {value: false, configurable: true}); window.dispatchEvent(new Event('offline'));});
      assert.equal(await ended.locator('.room-top .status').textContent(), 'Room closed');
      await ended.waitForFunction(() => document.title === 'flim · Room closed');
    });
  } finally {await ended.close();}
  for (const width of [280, 400, 680]) {
    const files = await browser.newPage({viewport: {width, height: 900}, reducedMotion: 'reduce'});
    try {
      await fixture(files, {restoreRoom: true, noLogs: width === 280}); await files.goto(url);
      await files.getByRole('button', {name: 'Files', exact: true}).click();
      await check(`Diagnostics remain available in Files and feedback follows visual reading order at ${width}px`, async () => {
        const logs = files.getByRole('button', {name: 'Open test logs', exact: true});
        assert.equal(await logs.isVisible(), true);
        if (width === 280) assert.equal(await files.getByText('Test logs are unavailable.', {exact: false}).isVisible(), true);
        else {await logs.click(); assert.ok(await files.evaluate(() => window.testCalls.includes('logs')));}
        assert.equal(await files.evaluate(() => Boolean(document.querySelector('#files-sidebar').compareDocumentPosition(document.querySelector('footer')) & Node.DOCUMENT_POSITION_FOLLOWING)), true);
        assert.equal(await files.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        await shot(files, `files-footer-${width}-0219`);
      });
    } finally {await files.close();}
  }
};
