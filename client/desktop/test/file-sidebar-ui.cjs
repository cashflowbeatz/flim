const {chromium} = require('playwright');
const http = require('node:http');
const path = require('node:path');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const dist = path.resolve(root, process.env.FLIM_FILES_TEST_DIST || 'dist-files');
const evidence = process.env.FLIM_FILES_TEST_DIST ? path.join(__dirname, 'evidence', require('../package.json').version, 'files') : path.join(__dirname, 'evidence/file-sync');
(async () => {
  await fs.mkdir(evidence, {recursive: true});
  const server = http.createServer(async (req, res) => {
    try {
      const pathname = new URL(req.url, 'http://127.0.0.1').pathname;
      const file = path.resolve(dist, '.' + (pathname === '/' ? '/index.html' : pathname));
      if (!file.startsWith(dist + path.sep)) {res.writeHead(403); res.end(); return;}
      const type = {'.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.woff2': 'font/woff2'}[path.extname(file)];
      res.setHeader('content-type', type || 'application/octet-stream'); res.end(await fs.readFile(file));
    } catch {res.writeHead(404); res.end();}
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const errors = [], passed = [];
  const browser = await chromium.launch({headless: true, executablePath: 'C:/Program Files/BraveSoftware/Brave-Browser/Application/brave.exe'});
  const check = async (name, task) => {await task(); passed.push(name); console.log('PASS ' + name);};
  const url = `http://127.0.0.1:${server.address().port}/`;
  async function visit(width, task, context = {}, options = {}) {
    const page = await browser.newPage({viewport: {width, height: 800}, reducedMotion: 'reduce', ...context});
    page.on('pageerror', error => errors.push(String(error)));
    await page.addInitScript(options => {
      window.fileCalls = []; window.folderFailures = false; window.fileFailures = false; window.deferFiles = false;
      const state = {platform: 'win32', name: 'Musician', installs: [{id: 'fl26', path: 'test-only', version: '26.1.7.5654', supported: true, reason: ''}], selectedInstallId: 'fl26',
        room: {code: '12345678', role: 'host', maxParticipants: 8, expiresAt: null, state: 'launched', status: 'active', peers: []}, busy: Boolean(options.busy), nativeReady: true, diagnosticsAvailable: !options.noLogs};
      if (options.noRoom) state.room = null;
      if (options.ended) state.room.status = 'ended';
      window.fileRoomState = state;
      window.sampleLocation = {path: 'D:\\Music\\Received samples', defaultPath: 'C:\\Users\\Producer\\Documents\\flim\\samples', custom: true, canChange: !state.room, error: ''};
      const make = (id, name, status, done, reason = '') => ({id, name, direction: status === 'uploading' ? 'upload' : 'download', state: status, reason, bytes: 100000, done, updatedAt: Date.now()});
      window.fileItems = [make('a', 'kick.wav', 'downloading', 50000), make('b', 'snare.wav', 'ready', 100000),
        make('c', 'clap.wav', 'failed', 0, 'verification'), make('d', 'hat.wav', 'blocked', 0, 'allowance')];
      window.flimDesktop = {
        getSampleLocation: async () => {
          if (window.locationReadFailure) throw new Error('Folder lookup failed');
          return {...window.sampleLocation, canChange: !state.room && !state.busy};
        },
        chooseSampleLocation: async () => {
          window.fileCalls.push('choose-folder');
          if (window.deferLocation) await new Promise(resolve => window.releaseLocation = resolve);
          if (window.locationFailure) throw new Error('The folder choice could not be saved.');
          if (!window.cancelLocation) window.sampleLocation.path = window.nextLocation || 'D:\\Audio\\New received samples';
          window.sampleLocation.custom = true; return {...window.sampleLocation, canChange: !state.room};
        },
        resetSampleLocation: async () => {window.fileCalls.push('reset-folder'); window.sampleLocation.path = window.sampleLocation.defaultPath; window.sampleLocation.custom = false; return {...window.sampleLocation, canChange: !state.room};},
        getState: async () => structuredClone(state), refreshRoom: async () => structuredClone(state),
        getFileTransfers: async () => {
          window.fileCalls.push('get'); const result = {items: structuredClone(window.fileItems), available: !window.fileUnavailable, stale: Boolean(window.fileStale), total: window.fileTotal, truncated: window.fileTruncated};
          if (window.deferFiles) await new Promise(resolve => window.releaseFiles = resolve);
          if (window.fileFailures) throw new Error('status unavailable'); return result;
        },
        openSampleFolder: async () => {window.fileCalls.push('folder'); if (window.folderFailures) throw new Error('The shared samples folder could not be opened.'); return {opened: true};},
        leaveRoom: async () => {state.room = null; return structuredClone(state);},
        openLogs: async () => {window.fileCalls.push('logs'); return {opened: true};},
        fitWindow: (height, width) => {if (window.failFit) throw new Error('Fit unavailable'); window.fileCalls.push({height, width});}, reportError: error => {window.fileCalls.push({error});},
      };
      if (options.noFilesApi) delete window.flimDesktop.getFileTransfers;
    }, options);
    try {await page.goto(url); await page.getByRole('button', {name: 'Files', exact: true}).waitFor(); await task(page);}
    catch (error) {console.error(await page.evaluate(() => window.fileCalls.filter(call => call.error))); await page.screenshot({path: path.join(evidence, 'failure.png'), fullPage: true}); throw error;}
    finally {await page.close();}
  }
  try {
    for (const width of [280, 400, 680]) await visit(width, async page => {
      await check(`Files edge arrow is available before setup at ${width}px without reading old-room transfers`, async () => {
        const toggle = page.getByRole('button', {name: 'Files', exact: true});
        assert.equal(await toggle.getAttribute('aria-expanded'), 'false');
        assert.equal(await toggle.getAttribute('title'), 'Show files sidebar');
        const box = await toggle.boundingBox(); assert.ok(box.width >= 44 && box.height >= 44);
        await toggle.focus(); await page.keyboard.press('Enter');
        await page.getByText('Create or join a room to share samples.', {exact: true}).waitFor();
        assert.equal(await page.getByRole('progressbar').count(), 0);
        assert.equal(await page.evaluate(() => window.fileCalls.filter(call => call === 'get').length), 0);
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        await page.screenshot({path: path.join(evidence, `before-room-${width}.png`), fullPage: true});
        await page.getByRole('button', {name: 'Open room setup', exact: true}).click();
        await page.getByLabel('Your name').waitFor();
        assert.equal(await page.locator('.files-sidebar').count(), 0);
        assert.equal(await page.getByLabel('Your name').evaluate(el => el === document.activeElement), true);
        await toggle.click(); await page.getByRole('button', {name: 'Hide files', exact: true}).focus(); await page.keyboard.press('Escape');
        assert.equal(await toggle.evaluate(el => el === document.activeElement), true);
      });
    }, {}, {noRoom: true});
    await visit(900, async page => {
      await check('Files stays collapsed until requested and does not poll in the background', async () => {
        assert.equal(await page.getByRole('complementary', {name: 'Shared files'}).count(), 0);
        assert.equal(await page.evaluate(() => window.fileCalls.filter(call => call === 'get').length), 0);
        await page.getByRole('button', {name: 'Files', exact: true}).click();
        await page.getByRole('complementary', {name: 'Shared files'}).waitFor();
        await page.waitForFunction(() => document.activeElement?.textContent === 'Files');
      });
      await check('The drawer renders real progress, ready files, verification failures and allowance status', async () => {
        await page.getByText('1 transferring · 0 waiting · 1 ready · 2 need attention', {exact: true}).waitFor();
        assert.equal(await page.getByRole('progressbar', {name: 'kick.wav: Downloading'}).getAttribute('value'), '50');
        assert.equal(await page.getByText('Ready on this computer', {exact: true}).isVisible(), true);
        assert.equal(await page.getByText('Download could not be verified', {exact: true}).isVisible(), true);
        assert.equal(await page.getByText('File allowance reached', {exact: true}).isVisible(), true);
        assert.ok((await page.locator('.files-sidebar').boundingBox()).width < 400);
        assert.ok((await page.locator('.app-shell').boundingBox()).width <= 680);
        assert.ok((await page.locator('.files-sidebar').boundingBox()).x > (await page.locator('.card-wrap').boundingBox()).x);
        await page.screenshot({path: path.join(evidence, 'desktop-files.png'), fullPage: true});
        await page.getByRole('region', {name: 'File transfer list', exact: true}).focus(); await page.keyboard.press('End');
        await page.waitForFunction(() => document.querySelector('.files-list').scrollTop > 0);
      });
      await check('Folder action works independently and reports failures in the drawer', async () => {
        await page.getByRole('button', {name: 'Open samples folder', exact: true}).click();
        assert.equal(await page.evaluate(() => window.fileCalls.filter(call => call === 'folder').length), 1);
        await page.evaluate(() => window.folderFailures = true);
        await page.getByRole('button', {name: 'Open samples folder', exact: true}).click();
        await page.getByRole('alert').filter({hasText: 'shared samples folder'}).waitFor();
      });
      await check('File-status failures preserve the confirmed list and recover automatically', async () => {
        await page.evaluate(() => window.fileFailures = true);
        await page.getByText('File status is temporarily unavailable.', {exact: false}).waitFor();
        assert.equal(await page.getByText('kick.wav', {exact: true}).isVisible(), true);
        await page.evaluate(() => {window.fileFailures = false; window.fileItems[0].state = 'ready'; window.fileItems[0].done = 100000;});
        await page.getByText('0 transferring · 0 waiting · 2 ready · 2 need attention', {exact: true}).waitFor();
        assert.equal(await page.getByText('File status is temporarily unavailable.', {exact: false}).count(), 0);
      });
      await check('Escape hides the drawer, restores the Files button and stops status polling', async () => {
        await page.getByRole('button', {name: 'Hide files', exact: true}).focus(); await page.keyboard.press('Escape');
        await page.waitForFunction(() => document.activeElement?.getAttribute('aria-controls') === 'files-sidebar');
        assert.equal(await page.getByRole('complementary', {name: 'Shared files'}).count(), 0);
        assert.equal(await page.getByRole('button', {name: 'Files', exact: true}).evaluate(el => el === document.activeElement), true);
        const before = await page.evaluate(() => window.fileCalls.filter(call => call === 'get').length);
        await page.clock.install(); await page.clock.fastForward(3001);
        assert.equal(await page.evaluate(() => window.fileCalls.filter(call => call === 'get').length), before);
      });
    });
    for (const width of [280, 375, 756, 1200]) await visit(width, async page => {
      await page.evaluate(() => window.fileItems.push({id: 'long', name: 'long-sample-name-'.repeat(20) + '.wav', direction: 'download', state: 'ready', reason: '', bytes: 1024, done: 1024, updatedAt: Date.now()}));
      await page.getByRole('button', {name: 'Files', exact: true}).click(); await page.getByText('snare.wav', {exact: true}).waitFor();
      await check(`File drawer reflows, scrolls and keeps controls usable at ${width}px`, async () => {
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        for (const name of ['Hide files', 'Open samples folder']) {
          const button = page.getByRole('button', {name, exact: true}); await button.scrollIntoViewIfNeeded(); const box = await button.boundingBox();
          assert.ok(box.height >= 44 && box.width >= 44);
        }
        if (width >= 600) {
          assert.ok((await page.locator('.files-sidebar').boundingBox()).x > (await page.locator('.card-wrap').boundingBox()).x);
          assert.equal(await page.locator('.room-overview').isVisible(), true);
        } else assert.equal(await page.locator('.room-overview').isVisible(), false);
        await page.screenshot({path: path.join(evidence, `files-${width}.png`), fullPage: true});
        await page.getByRole('button', {name: 'Hide files', exact: true}).click(); assert.equal(await page.locator('.files-sidebar').count(), 0);
      });
    });
    await visit(900, async page => {
      await check('An unfinished status read cannot restore a panel after returning to setup', async () => {
        await page.evaluate(() => window.deferFiles = true);
        await page.getByRole('button', {name: 'Files', exact: true}).click(); await page.waitForFunction(() => Boolean(window.releaseFiles));
        await page.getByRole('button', {name: 'Return to start', exact: true}).click(); await page.getByLabel('Your name').waitFor();
        await page.evaluate(() => window.releaseFiles());
        assert.equal(await page.locator('.files-sidebar').count(), 0);
      });
    });
    await visit(375, async page => {
      await page.evaluate(() => window.fileItems = []);
      await page.getByRole('button', {name: 'Files', exact: true}).click();
      await check('Empty state explains how to share and keyboard focus remains visible in forced colors', async () => {
        await page.getByText('Add a sample to a shared channel or playlist', {exact: false}).waitFor();
        const button = page.getByRole('button', {name: 'Hide files', exact: true}); await button.focus(); await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
        assert.equal(await button.evaluate(el => getComputedStyle(el).outlineStyle), 'solid');
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      });
    }, {forcedColors: 'active'});
    await visit(900, async page => {
      await page.evaluate(() => {window.fileItems = [{id: 'retry', name: 'pending-kick.wav', direction: 'download', state: 'waiting', reason: 'retrying', bytes: 12000, done: 0, updatedAt: Date.now()}];});
      await page.getByRole('button', {name: 'Files', exact: true}).click();
      await check('Pending edits explain automatic retry beside usable room controls in a compact sidebar', async () => {
        await page.getByText('Waiting to retry automatically', {exact: true}).waitFor();
        await page.getByText('no extra sample edit is needed.', {exact: false}).waitFor();
        assert.ok((await page.locator('.app-shell').boundingBox()).width <= 680);
        assert.equal(await page.locator('.room-overview').isVisible(), true);
        await page.screenshot({path: path.join(evidence, 'pending-file.png'), fullPage: true});
        await page.getByRole('button', {name: 'Files', exact: true}).click();
        assert.equal(await page.locator('.room-overview').isVisible(), true);
        assert.equal(await page.locator('.files-sidebar').count(), 0);
        assert.equal(await page.getByRole('button', {name: 'Files', exact: true}).getAttribute('aria-expanded'), 'false');
      });
    });
    await require('./file-sidebar-refinements-0218.cjs')({visit, check, evidence});
    await require('./file-sidebar-refinements-0219.cjs')({visit, check, evidence});
    await require('./file-sidebar-refinements-0221.cjs')({visit, check, evidence});
    await require('./file-sidebar-refinements-0222.cjs')({visit, check, evidence});
    assert.deepEqual(errors, []);
    await fs.writeFile(path.join(evidence, 'ui.json'), JSON.stringify({passed: passed.length, checks: passed, errors}, null, 2));
  } finally {await browser.close(); await new Promise(resolve => server.close(resolve));}
})().catch(error => {console.error(error); process.exitCode = 1;});
