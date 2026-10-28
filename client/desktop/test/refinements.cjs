const assert = require('node:assert/strict');
const {version} = require('../package.json');
const installs = require('./install-picker.cjs');

module.exports = async ({browser, url, check, fixture, open, shot}) => {
  async function visit(options, task, context = {}) {
    const page = await browser.newPage(context);
    try { await fixture(page, options); await open(page, url); await task(page); }
    catch (error) { try { await shot(page, 'refinement-failure'); } catch {} throw error; }
    finally { await page.close(); }
  }
  async function create(page) {
    await page.getByLabel('Your name').fill('Room test');
    await page.getByRole('button', {name: 'Create room & open FL Studio'}).click();
    await page.getByText('Studio friend', {exact: true}).waitFor();
  }
  const calls = (page, label) => page.evaluate(label => window.testCalls.filter(call => call === label).length, label);

  await visit({deferSetup: true}, async page => {
    await check('Delayed setup announces discovery and disables actions without claiming native files are missing', async () => {
      assert.equal(await page.getByText('Checking your FL Studio installations…', {exact: true}).isVisible(), true);
      assert.equal(await page.getByLabel('Your name').isDisabled(), true);
      assert.equal(await page.getByText('The FL Studio launcher is missing.', {exact: false}).count(), 0);
      await page.evaluate(() => window.testReleaseSetup());
      await page.getByLabel('Your name').waitFor({state: 'visible'});
      await page.waitForFunction(() => !document.querySelector('#room-name').disabled);
      assert.equal(await page.getByLabel('Your name').evaluate(el => el === document.activeElement), true);
    });
  });

  await visit({setupFailures: 1}, async page => {
    await check('Setup errors have a working retry and diagnostics remain reachable', async () => {
      await page.getByRole('button', {name: 'Retry setup'}).waitFor();
      assert.equal(await page.getByText('The FL Studio launcher is missing.', {exact: false}).count(), 0);
      await page.getByRole('button', {name: 'Open test logs', exact: true}).click();
      assert.equal(await calls(page, 'logs'), 1);
      await page.getByRole('button', {name: 'Retry setup'}).click();
      await page.waitForFunction(() => !document.querySelector('#room-name').disabled);
      assert.equal(await page.getByRole('button', {name: 'Retry setup'}).count(), 0);
      assert.equal(await page.getByLabel('Your name').evaluate(el => el === document.activeElement), true);
    });
  });

  await visit({}, async page => {
    await check('Blank and punctuation-only names get linked inline errors before any room request', async () => {
      for (const invalid of ['   ', '---', '🎹']) {
        await page.getByLabel('Your name').fill(invalid);
        await page.getByRole('button', {name: 'Create room & open FL Studio'}).click();
        assert.equal(await page.getByLabel('Your name').getAttribute('aria-invalid'), 'true');
        assert.match(await page.locator('#name-error').textContent(), /at least one letter/);
        assert.match(await page.getByLabel('Your name').getAttribute('aria-describedby'), /name-error/);
        assert.equal(await page.getByLabel('Your name').evaluate(el => el === document.activeElement), true);
      }
      assert.equal(await calls(page, 'host'), 0);
      await page.getByLabel('Your name').fill('Maya');
      assert.equal(await page.locator('#name-error').count(), 0);
      assert.equal(await page.locator('.field-counter').textContent(), '4/24');
      await page.getByLabel('Your name').fill('A'.repeat(40));
      assert.equal((await page.getByLabel('Your name').inputValue()).length, 24);
    });
    await check('Mode choices support arrows, Home and End and point to the next field', async () => {
      const first = page.getByRole('tab', {name: 'Create a room', exact: true});
      const second = page.getByRole('tab', {name: 'Join a room', exact: true});
      await first.focus(); await page.keyboard.press('ArrowRight');
      assert.equal(await second.getAttribute('aria-selected'), 'true');
      assert.equal(await second.evaluate(el => el === document.activeElement), true);
      await page.keyboard.press('Home'); assert.equal(await first.getAttribute('aria-selected'), 'true');
      await page.keyboard.press('End'); assert.equal(await second.getAttribute('aria-selected'), 'true');
      await first.click(); await second.click();
      await page.waitForFunction(() => document.activeElement?.id === 'room-code');
      assert.equal(await page.getByLabel('Room number').evaluate(el => el === document.activeElement), true);
    });
    await check('Incomplete room numbers focus a linked error, show digit progress and never contact the relay', async () => {
      await page.getByLabel('Room number').fill('1234-567');
      await page.getByRole('button', {name: 'Join room & open FL Studio'}).click();
      assert.equal(await calls(page, 'member'), 0);
      assert.equal(await page.getByLabel('Room number').getAttribute('aria-invalid'), 'true');
      assert.match(await page.locator('#code-help').textContent(), /7 of 8/);
      assert.equal(await page.getByLabel('Room number').evaluate(el => el === document.activeElement), true);
      await page.getByLabel('Room number').fill('1234-5678');
      assert.equal(await page.locator('#code-error').count(), 0);
    });
    await check('Paste room number formats eight digits and restores input focus', async () => {
      await page.getByRole('button', {name: 'Paste room number', exact: true}).click();
      assert.equal(await page.getByLabel('Room number').inputValue(), '8765 4321');
      assert.equal(await page.getByLabel('Room number').evaluate(el => el === document.activeElement), true);
      assert.equal(await calls(page, 'paste'), 1);
      assert.match(await page.locator('#code-help').textContent(), /8 of 8/);
    });
    await check('A late paste cannot overwrite newer typing or a different mode visit', async () => {
      await page.evaluate(() => { window.testDeferPaste = true; });
      await page.getByRole('button', {name: 'Paste room number', exact: true}).click();
      await page.waitForFunction(() => Boolean(window.testReleasePaste));
      await page.getByLabel('Room number').fill('1111 2222');
      await page.evaluate(() => window.testReleasePaste());
      await page.getByRole('button', {name: 'Paste room number', exact: true}).waitFor();
      assert.equal(await page.getByLabel('Room number').inputValue(), '1111 2222');
      await page.evaluate(() => { window.testReleasePaste = null; });
      await page.getByRole('button', {name: 'Paste room number', exact: true}).click();
      await page.waitForFunction(() => Boolean(window.testReleasePaste));
      await page.getByRole('tab', {name: 'Create a room', exact: true}).click();
      await page.getByRole('tab', {name: 'Join a room', exact: true}).click();
      await page.evaluate(() => window.testReleasePaste());
      await page.getByRole('button', {name: 'Paste room number', exact: true}).waitFor();
      assert.equal(await page.getByLabel('Room number').inputValue(), '1111 2222');
    });
    await shot(page, 'refined-validation-join');
  });

  await visit({pasteError: true}, async page => {
    await check('Invalid clipboard contents leave the typed number intact and explain the next step', async () => {
      await page.getByRole('tab', {name: 'Join a room', exact: true}).click();
      await page.getByLabel('Room number').fill('1111 2222');
      await page.getByRole('button', {name: 'Paste room number', exact: true}).click();
      await page.locator('#code-error').waitFor();
      assert.equal(await page.getByLabel('Room number').inputValue(), '1111 2222');
      assert.match(await page.locator('#code-error').textContent(), /Copy an eight-digit/);
      assert.equal(await calls(page, 'member'), 0);
    });
  });

  await visit({}, async page => {
    await check('Checking installations preserves typed name and selection and reports the scan result', async () => {
      await page.getByLabel('Your name').fill('Keep this name');
      await installs.select(page, 'fl26');
      await page.evaluate(() => { window.testDeferRescan = true; });
      await page.getByRole('button', {name: 'Check again', exact: true}).click();
      await page.waitForFunction(() => Boolean(window.testReleaseRescan));
      assert.equal(await page.getByLabel('Your name').inputValue(), 'Keep this name');
      assert.equal(await installs.value(page), 'fl26');
      assert.equal(await page.getByLabel('FL Studio', {exact: true}).isDisabled(), true);
      await page.evaluate(() => window.testReleaseRescan());
      await page.getByText('Found 4 installations. Compatibility was checked again.', {exact: true}).waitFor();
      assert.equal(await page.getByLabel('Your name').inputValue(), 'Keep this name');
      assert.equal(await calls(page, 'rescan'), 1);
      assert.equal(await calls(page, 'host'), 0);
    });
  });

  await visit({rescanError: true}, async page => {
    await check('Failed scans restore controls, focus the error and clear stale errors when edited', async () => {
      await page.getByLabel('Your name').fill('Keep this name');
      await page.getByRole('button', {name: 'Check again', exact: true}).click();
      const error = page.getByRole('alert'); await error.waitFor();
      assert.equal(await error.evaluate(el => el.contains(document.activeElement)), true);
      assert.equal(await page.getByRole('button', {name: 'Check again', exact: true}).isEnabled(), true);
      assert.equal(await page.getByLabel('Your name').inputValue(), 'Keep this name');
      await page.getByLabel('Your name').fill('Changed name');
      assert.equal(await page.getByRole('alert').count(), 0);
    });
  });

  await visit({}, async page => {
    await check('Offline detection disables new room requests immediately and reconnect restores the form', async () => {
      await page.getByLabel('Your name').fill('Room test');
      await page.evaluate(() => {
        window.testOnline = false;
        Object.defineProperty(navigator, 'onLine', {configurable: true, get: () => window.testOnline});
        window.dispatchEvent(new Event('offline'));
      });
      await page.getByText('You are offline.', {exact: false}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Create room & open FL Studio'}).isDisabled(), true);
      assert.equal(await calls(page, 'host'), 0);
      await page.evaluate(() => { window.testOnline = true; window.dispatchEvent(new Event('online')); });
      assert.equal(await page.getByRole('button', {name: 'Create room & open FL Studio'}).isEnabled(), true);
    });
    await create(page);
    await check('Room title, people freshness, major release labels and mixed-version help reflect current state', async () => {
      assert.equal(await page.title(), 'flim · Room is open');
      assert.equal(await page.getByText('People updated just now', {exact: true}).isVisible(), true);
      assert.equal(await page.locator('.people small').first().textContent(), 'FL Studio 25');
      assert.equal(await page.locator('.people small').first().getAttribute('aria-label'), 'FL Studio 25, build 25.2.5.5319');
      assert.match(await page.locator('.compatibility-note').textContent(), /some sounds and playlist features may be unavailable/);
      assert.equal(await page.getByLabel('2 of 8 people connected').isVisible(), true);
    });
    await check('Collapsed room status stays visible and Your room returns directly to the current room with focus', async () => {
      await page.getByRole('button', {name: 'Collapse room controls', exact: true}).click();
      assert.equal(await page.locator('.hero-caption').textContent(), 'Room is open · 2 people');
      assert.equal(await page.getByText(version, {exact: true}).isVisible(), true);
      await page.getByRole('button', {name: 'Your room', exact: true}).click();
      await page.waitForFunction(() => document.activeElement?.classList.contains('room-number') || document.activeElement?.matches('.room-view h2'));
      assert.equal(await page.getByRole('heading', {name: 'Room 1234 5678', exact: true}).evaluate(el => el === document.activeElement), true);
      assert.equal(await page.getByText('Menu', {exact: true}).count(), 0);
    });
    await check('Copying an invitation does not interrupt an in-flight people refresh or clear copy feedback', async () => {
      await page.evaluate(() => { window.testDeferNextStatus = true; });
      await page.getByRole('button', {name: 'Refresh people', exact: true}).click();
      await page.waitForFunction(() => Boolean(window.testReleaseStatus));
      await page.getByRole('button', {name: 'Copy room number', exact: true}).click();
      await page.getByRole('button', {name: 'Room number copied', exact: true}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Refreshing people…', exact: true}).isDisabled(), true);
      assert.equal(await page.getByRole('button', {name: 'Return to start', exact: true}).isEnabled(), true);
      assert.equal(await page.getByRole('status').filter({hasText: 'Room number copied to clipboard.'}).count(), 1);
      await page.evaluate(() => window.testReleaseStatus());
      await page.getByRole('button', {name: 'Refresh people', exact: true}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Room number copied', exact: true}).isVisible(), true);
    });
    await check('A delayed copy cannot announce success in a later visit to the same numbered room', async () => {
      await page.evaluate(() => { window.testDeferCopy = true; });
      await page.getByRole('button', {name: 'Room number copied', exact: true}).click();
      await page.waitForFunction(() => Boolean(window.testReleaseCopy));
      await page.getByRole('button', {name: 'Return to start', exact: true}).click();
      await create(page);
      await page.evaluate(() => window.testReleaseCopy());
      await page.getByRole('button', {name: 'Copy room number', exact: true}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Room number copied', exact: true}).count(), 0);
    });
  });

  await visit({copyError: true}, async page => {
    await create(page);
    await check('Copy failures keep room navigation usable and offer a selectable room number', async () => {
      await page.getByRole('button', {name: 'Copy room number', exact: true}).click();
      await page.getByText('You can select and copy the room number above.', {exact: false}).waitFor();
      assert.equal(await page.locator('.room-number').evaluate(el => getComputedStyle(el).userSelect), 'all');
      assert.equal(await page.getByRole('button', {name: 'Return to start', exact: true}).isEnabled(), true);
    });
  });

  await visit({}, async page => {
    await page.clock.install(); await create(page);
    await check('Active-room status failure marks the people list stale and retries after five seconds', async () => {
      await page.evaluate(() => { window.testFailStatus = true; });
      await page.getByRole('button', {name: 'Refresh people', exact: true}).click();
      await page.getByRole('button', {name: 'Retry status', exact: true}).waitFor();
      assert.equal(await page.locator('.people li').count(), 2);
      assert.match(await page.locator('.list-freshness').textContent(), /last confirmed people/);
      const before = await calls(page, 'refresh');
      await page.evaluate(() => { window.testFailStatus = false; });
      await page.clock.fastForward(5001);
      await page.getByText('People updated just now', {exact: true}).waitFor();
      assert.equal(await calls(page, 'refresh'), before + 1);
      assert.equal(await page.getByRole('button', {name: 'Retry status', exact: true}).count(), 0);
    });
    await check('Offline rooms retain confirmed people, expose stale status and resume automatically', async () => {
      const before = await calls(page, 'refresh');
      await page.evaluate(() => {
        window.testOnline = false;
        Object.defineProperty(navigator, 'onLine', {configurable: true, get: () => window.testOnline});
        window.dispatchEvent(new Event('offline'));
      });
      assert.equal(await page.getByRole('button', {name: 'Refresh people', exact: true}).isDisabled(), true);
      assert.equal(await page.title(), 'flim · Offline');
      assert.equal(await page.locator('.people li').count(), 2);
      await page.clock.fastForward(60_001);
      assert.equal(await calls(page, 'refresh'), before);
      await page.evaluate(() => { window.testOnline = true; window.dispatchEvent(new Event('online')); });
      await page.waitForFunction(before => window.testCalls.filter(call => call === 'refresh').length > before, before);
      assert.equal(await page.title(), 'flim · Room is open');
    });
  });

  await visit({}, async page => {
    await create(page);
    await check('Full rooms scroll inside the component, sort consistently and isolate international names', async () => {
      await page.evaluate(() => { window.testPeers = [
        {id: 'z', name: 'Z'.repeat(64), role: 'member', fl: '26.1.7.5654'},
        {id: 'accent', name: 'e\u0301clair', role: 'member', fl: '25.2.5.5319'},
        {id: 'host', name: 'Owner', role: 'host', fl: '24.2.2.4597'},
        {id: 'rtl', name: 'منتج موسيقى', role: 'member', fl: '25.2.5.5319'},
        ...[2, 10, 1, 3].map(n => ({id: `friend-${n}`, name: `Friend ${n}`, role: 'member', fl: '26.1.7.5654'})),
      ]; });
      await page.getByRole('button', {name: 'Refresh people', exact: true}).click();
      const list = page.getByRole('region', {name: 'In this room', exact: true});
      assert.equal(await list.getAttribute('tabindex'), '0');
      assert.equal(await page.getByText('Scroll for everyone', {exact: true}).isVisible(), true);
      const size = await list.evaluate(el => ({height: el.clientHeight, total: el.scrollHeight}));
      assert.ok(size.height <= 216 && size.total > size.height);
      assert.equal(await page.locator('.people li').first().locator('bdi').textContent(), 'Owner');
      const names = await page.locator('.people li bdi').allTextContents();
      assert.ok(names.indexOf('Friend 2') < names.indexOf('Friend 10'));
      assert.equal(await page.locator('.people li').filter({hasText: 'e\u0301clair'}).locator('bdi').textContent(), 'e\u0301clair');
      assert.equal(await page.locator('.person-initial').count(), 0);
      assert.equal(await page.locator('.people li bdi').first().evaluate(el => getComputedStyle(el).unicodeBidi), 'isolate');
      await list.focus(); await page.keyboard.press('End');
      await page.waitForFunction(() => document.querySelector('.people-scroll')?.scrollTop > 0, undefined, {timeout: 2000});
      assert.ok(await list.evaluate(el => el.scrollTop > 0));
      await page.evaluate(() => window.testPeers.reverse());
      await page.getByRole('button', {name: 'Refresh people', exact: true}).click();
      assert.deepEqual(await page.locator('.people li bdi').allTextContents(), names);
      await list.evaluate(el => { el.scrollTop = 0; });
      await shot(page, 'refined-full-room');
    });
  }, {viewport: {width: 375, height: 650}});

  await visit({ended: true}, async page => {
    await page.evaluate(() => { window.testRoomState = 'uncertain'; });
    await page.getByLabel('Your name').fill('Room test');
    await page.getByRole('button', {name: 'Create room & open FL Studio'}).click();
    await check('Closed rooms suppress obsolete startup warnings and prevent reopening a dead room', async () => {
      await page.getByText('Room closed', {exact: true}).waitFor();
      assert.equal(await page.getByText('The launcher did not confirm startup.', {exact: false}).count(), 0);
      assert.equal(await page.getByRole('button', {name: 'Open FL Studio', exact: true}).count(), 0);
      assert.equal(await page.getByRole('button', {name: 'Refresh people', exact: true}).count(), 0);
      assert.equal(await page.getByRole('button', {name: 'Return to start', exact: true}).isEnabled(), true);
    });
  });

  await visit({logsError: true}, async page => {
    await check('Short-window footer logs and room entry remain reachable without Menu', async () => {
      await page.getByRole('button', {name: 'Collapse room controls', exact: true}).click();
      assert.equal(await page.getByText(version, {exact: true}).isVisible(), true);
      await page.getByRole('button', {name: 'Open test logs', exact: true}).click();
      await page.getByText('The test logs folder could not be opened.', {exact: true}).waitFor();
      await page.getByRole('button', {name: 'Get started', exact: true}).click();
      await page.waitForFunction(() => document.activeElement?.id === 'room-name');
      assert.equal(await page.getByLabel('Your name').evaluate(el => el === document.activeElement), true);
      assert.equal(await page.getByText('Menu', {exact: true}).count(), 0);
    });
  }, {viewport: {width: 280, height: 200}});

  for (const width of [280, 320, 375, 600, 810, 1200]) {
    await visit({}, async page => {
      await page.getByRole('tab', {name: 'Join a room', exact: true}).click();
      await check(`Refined form and installation disclosures remain usable at ${width}px`, async () => {
        await page.getByLabel('Your name').fill('Room test');
        await page.evaluate(() => { window.testChooseId = 'unknown'; });
        await page.getByRole('button', {name: 'Choose folder', exact: true}).click();
        await page.locator('.install-details summary').click();
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        for (const name of ['Paste room number', 'Choose folder', 'Check again']) {
          const rect = await page.getByRole('button', {name, exact: true}).boundingBox();
          assert.ok(rect.height >= 44 && rect.width >= 44, `${name}: ${JSON.stringify(rect)}`);
        }
        assert.ok(await page.locator('.room-form .form-footnote').evaluate(el => parseFloat(getComputedStyle(el).fontSize) >= 12));
        assert.equal(await page.getByLabel('Room number').getAttribute('enterkeyhint'), 'go');
        await shot(page, `refined-join-${width}`);
      });
    }, {viewport: {width, height: 600}, reducedMotion: 'reduce'});
  }

  await visit({}, async page => {
    await check('Forced-colors mode preserves selection boundaries, focus and readable disabled controls', async () => {
      const mode = page.getByRole('tab', {name: 'Create a room', exact: true});
      await mode.focus();
      await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
      const style = await mode.evaluate(el => {
        const s = getComputedStyle(el); return {border: s.borderTopWidth, outline: s.outlineStyle, color: s.color};
      });
      assert.equal(style.border, '1px'); assert.equal(style.outline, 'solid');
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await shot(page, 'refined-forced-colors');
    });
  }, {viewport: {width: 320, height: 700}, forcedColors: 'active', reducedMotion: 'reduce'});

  await visit({}, async page => {
    await page.clock.install();
    await check('Long operations explain that they are still pending and do not enable duplicate actions', async () => {
      await page.evaluate(() => { window.testDeferRescan = true; });
      await page.getByRole('button', {name: 'Check again', exact: true}).click();
      await page.waitForFunction(() => Boolean(window.testReleaseRescan));
      await page.clock.fastForward(8001);
      assert.equal(await page.locator('.slow-operation').isVisible(), true);
      assert.equal(await page.getByRole('button', {name: 'Check again', exact: true}).isDisabled(), true);
      assert.equal(await calls(page, 'rescan'), 1);
      await page.evaluate(() => window.testReleaseRescan());
      await page.getByText('Found 4 installations.', {exact: false}).waitFor();
      assert.equal(await page.locator('.slow-operation').count(), 0);
    });
  });

  {
    const page = await browser.newPage();
    try {
      await fixture(page, {badState: true}); await page.goto(url);
      await check('A rendering failure offers reload and local logs and can recover without a room action', async () => {
        await page.getByRole('button', {name: 'Reload interface', exact: true}).waitFor();
        assert.ok(await page.evaluate(() => window.testErrors.some(item => /find/.test(item.message))));
        await page.getByRole('button', {name: 'Open test logs', exact: true}).click();
        assert.equal(await calls(page, 'logs'), 1);
        assert.equal(await calls(page, 'leave'), 0); assert.equal(await calls(page, 'launch'), 0);
        await page.getByRole('button', {name: 'Reload interface', exact: true}).click();
        await page.getByRole('button', {name: 'Get started', exact: true}).waitFor();
        assert.equal(await page.getByRole('button', {name: 'Reload interface', exact: true}).count(), 0);
      });
    } finally { await page.close(); }
  }
};
