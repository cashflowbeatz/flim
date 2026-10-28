'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {checkDirectory, validDirectory} = require('../electron/sample-location.cjs');
const {DesktopBridge} = require('../electron/bridge-core.cjs');

async function fixture(t) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'flim-folder-'));
  t.after(async () => {assert.ok(root.startsWith(os.tmpdir() + path.sep)); await fs.rm(root, {recursive: true, force: true});});
  const selected = path.join(root, 'Received audio'); await fs.mkdir(selected);
  const bridge = new DesktopBridge({nativeDir: root, preferencesFile: path.join(root, 'prefs.json'), platform: 'none',
    defaultSampleDirectory: path.join(root, 'Redirected Documents', 'flim', 'samples'), chooseSampleDirectory: async () => selected});
  await bridge.initialize();
  return {bridge, root, selected};
}

test('sample locations accept absolute drive and UNC folders and reject device, relative, malformed and overlong paths', () => {
  for (const value of ['C:\\Audio', 'D:\\音楽\\Shared files', '\\\\studio\\Audio\\Shared', 'C:\\']) assert.equal(validDirectory(value), true, value);
  for (const value of ['', '../Audio', 'C:Audio', '\\Audio', '\\\\?\\C:\\Audio', '\\\\.\\pipe', 'C:\\a\n', 'C:\\bad?', 'C:\\path:stream', 'C:\\bad.', 'C:\\bad ', 'C:\\' + 'x'.repeat(178), null]) assert.equal(validDirectory(value), false, String(value));
});
test('folder checks verify actual writes and leave no probe behind or changes to existing audio', async t => {
  const f = await fixture(t); await fs.writeFile(path.join(f.selected, 'kick.wav'), 'audio');
  assert.equal(await checkDirectory(f.selected), await fs.realpath(f.selected));
  assert.deepEqual(await fs.readdir(f.selected), ['kick.wav']); assert.equal(await fs.readFile(path.join(f.selected, 'kick.wav'), 'utf8'), 'audio');
});
test('files and missing custom folders cannot become sample destinations', async t => {
  const f = await fixture(t); const file = path.join(f.root, 'file.wav'); await fs.writeFile(file, 'audio');
  await assert.rejects(checkDirectory(file), /Choose a folder/);
  await assert.rejects(checkDirectory(path.join(f.root, 'missing')), {code: 'ENOENT'});
  assert.equal(await fs.readFile(file, 'utf8'), 'audio');
});
test('write failures close and remove only the owned probe before reporting failure', async t => {
  const f = await fixture(t); let opened, closed = false;
  const io = {...fs, open: async (...args) => {
    const handle = await fs.open(...args); opened = args[0];
    return {writeFile: async () => {throw Object.assign(new Error('disk full'), {code: 'ENOSPC'});}, sync: () => handle.sync(), close: async () => {closed = true; await handle.close();}};
  }};
  await assert.rejects(checkDirectory(f.selected, {io}), /disk full/); assert.equal(closed, true);
  await assert.rejects(fs.stat(opened), {code: 'ENOENT'});
});
test('failed exclusive probe creation never removes an existing file', async t => {
  const f = await fixture(t); let deletes = 0;
  const io = {...fs, open: async () => {throw Object.assign(new Error('denied'), {code: 'EACCES'});}, unlink: async () => {deletes++;}};
  await assert.rejects(checkDirectory(f.selected, {io}), /denied/); assert.equal(deletes, 0);
});
test('resolved path limits are checked after following folder links', async t => {
  const f = await fixture(t); let writes = 0;
  await assert.rejects(checkDirectory(f.selected, {io: {...fs, realpath: async () => 'D:\\' + 'x'.repeat(180), open: async () => {writes++;}}}), /actual folder path/);
  assert.equal(writes, 0);
});
test('folder junctions save the actual destination and probe that destination only', async t => {
  const f = await fixture(t); const linked = path.join(f.root, 'linked'); await fs.symlink(f.selected, linked, 'junction');
  assert.equal(await checkDirectory(linked), await fs.realpath(f.selected));
  assert.deepEqual(await fs.readdir(f.selected), []);
});
test('chosen folder persists privately and restores with the same destination after reopening', async t => {
  const f = await fixture(t); const location = await f.bridge.changeSampleLocation();
  assert.equal(location.path, await fs.realpath(f.selected)); assert.equal(location.custom, true); assert.equal(location.canChange, true);
  const saved = JSON.parse(await fs.readFile(f.bridge.preferencesFile, 'utf8')); assert.equal(saved.sampleDirectory, location.path);
  f.bridge.sampleDirectory = ''; await f.bridge.initialize(); assert.equal(f.bridge.getSampleLocation().path, location.path);
  assert.equal(JSON.stringify(f.bridge.state()).includes(f.selected), false);
});
test('canceling a folder dialog preserves both the previous location and its preferences bytes', async t => {
  const f = await fixture(t); await f.bridge.changeSampleLocation(); const previous = await fs.readFile(f.bridge.preferencesFile, 'utf8');
  f.bridge.chooseSampleDirectory = async () => null;
  assert.equal((await f.bridge.changeSampleLocation()).path, await fs.realpath(f.selected));
  assert.equal(await fs.readFile(f.bridge.preferencesFile, 'utf8'), previous);
});
test('a failed settings replacement rolls back folder choice and preserves the previous saved choice', async t => {
  const f = await fixture(t); await f.bridge.changeSampleLocation(); const previous = await fs.readFile(f.bridge.preferencesFile, 'utf8');
  const second = path.join(f.root, 'second'); await fs.mkdir(second); f.bridge.chooseSampleDirectory = async () => second;
  f.bridge.io = {...fs, rename: async () => {throw new Error('denied');}};
  await assert.rejects(f.bridge.changeSampleLocation(), /previous samples folder/);
  assert.equal(f.bridge.getSampleLocation().path, await fs.realpath(f.selected)); assert.equal(await fs.readFile(f.bridge.preferencesFile, 'utf8'), previous);
});
test('default reset uses redirected Documents, removes the custom setting and never moves old samples', async t => {
  const f = await fixture(t); await f.bridge.changeSampleLocation(); await fs.writeFile(path.join(f.selected, 'keep.wav'), 'original');
  const location = await f.bridge.changeSampleLocation(true); assert.equal(location.custom, false); assert.equal(location.path, f.bridge.defaultSampleDirectory);
  assert.equal(Object.hasOwn(JSON.parse(await fs.readFile(f.bridge.preferencesFile, 'utf8')), 'sampleDirectory'), false);
  assert.equal(await fs.readFile(path.join(f.selected, 'keep.wav'), 'utf8'), 'original'); assert.deepEqual(await fs.readdir(location.path), []);
});
test('an active room locks folder changes and its private launch path stays out of general room state', async t => {
  const f = await fixture(t); f.bridge.room = {code: '12345678', sampleDirectory: f.selected};
  assert.equal(f.bridge.getSampleLocation().canChange, false); assert.equal(f.bridge.getSampleLocation().path, f.selected);
  await assert.rejects(f.bridge.changeSampleLocation(), /Return to start/); await assert.rejects(f.bridge.changeSampleLocation(true), /Return to start/);
  assert.equal(Object.hasOwn(f.bridge.state().room, 'sampleDirectory'), false);
});
test('duplicate choices and room actions cannot overlap a native folder dialog', async t => {
  const f = await fixture(t); let release;
  f.bridge.chooseSampleDirectory = () => new Promise(resolve => {release = resolve;});
  const first = f.bridge.changeSampleLocation(); assert.equal(f.bridge.getSampleLocation().canChange, false);
  await assert.rejects(f.bridge.changeSampleLocation(), /current step/); await assert.rejects(f.bridge.createRoom({}), /current step/);
  release(null); await first; assert.equal(f.bridge.busy, false);
});
test('a vanished custom drive never silently falls back to the default folder', async t => {
  const f = await fixture(t); await f.bridge.changeSampleLocation(); await fs.rmdir(f.selected);
  await assert.rejects(f.bridge.checkedSampleDirectory(), /no longer available/);
  await assert.rejects(fs.stat(f.bridge.defaultSampleDirectory), {code: 'ENOENT'}); assert.equal(f.bridge.getSampleLocation().custom, true);
});
test('invalid saved locations block launch preparation until explicitly repaired and are not erased by other saves', async t => {
  const f = await fixture(t); await fs.writeFile(f.bridge.preferencesFile, JSON.stringify({sampleDirectory: 'relative-path'}));
  await f.bridge.initialize(); assert.match(f.bridge.getSampleLocation().error, /saved samples folder/);
  await assert.rejects(f.bridge.checkedSampleDirectory(), /saved samples folder/); assert.equal(await f.bridge.persist(), false);
  assert.equal(JSON.parse(await fs.readFile(f.bridge.preferencesFile, 'utf8')).sampleDirectory, 'relative-path');
  await f.bridge.changeSampleLocation(); assert.equal(f.bridge.getSampleLocation().error, '');
});
test('opening a chosen folder does not create another location or require another write probe', async t => {
  const f = await fixture(t); await f.bridge.changeSampleLocation();
  f.bridge.io = {...fs, open: async () => {throw new Error('write forbidden');}};
  assert.equal(await f.bridge.checkedSampleDirectory({writable: false}), await fs.realpath(f.selected));
  await assert.rejects(f.bridge.checkedSampleDirectory(), /unavailable/);
});
test('corrupt preferences cannot silently erase a custom destination on the next settings save', async t => {
  const f = await fixture(t); await fs.writeFile(f.bridge.preferencesFile, '{broken'); await f.bridge.initialize();
  assert.match(f.bridge.getSampleLocation().error, /could not be recovered/);
  assert.equal(await f.bridge.persist(), false); assert.equal(await fs.readFile(f.bridge.preferencesFile, 'utf8'), '{broken');
  await f.bridge.changeSampleLocation(true); assert.equal(f.bridge.getSampleLocation().error, '');
  assert.equal(f.bridge.getSampleLocation().custom, false);
});
test('progress rejects nonfinite byte counts and local storage waits have a specific status', async () => {
  const {fileProgress, fileStatus} = await import('../src/utils/fileTransfers.js');
  for (const [bytes, done] of [[1, NaN], [Infinity, 10], [10, Infinity], [-1, 10]]) assert.equal(fileProgress({bytes, done}), 0);
  assert.equal(fileStatus({state: 'waiting', reason: 'local_cache'}), 'Waiting for folder access');
});


test('sample destinations reject reserved Windows device components and malformed Unicode before disk access', async () => {
  for (const value of ['C:/Audio/CON', 'C:/Audio/nul.wav', 'D:/COM1/Shared', 'D:/LPT\u00b9/Shared', 'C:/Audio/\ud800']) assert.equal(validDirectory(value), false, value);
  assert.equal(validDirectory('C:/Audio/Console'), true);
  assert.equal(validDirectory('C:/Audio/\ud83c\udfb9'), true);
});
test('probe cleanup does not hide the original write or sync failure', async () => {
  const diskFull = Object.assign(new Error('disk full'), {code: 'ENOSPC'});
  let closes = 0, deletes = 0;
  const io = {realpath: async value => value, stat: async () => ({isDirectory: () => true}),
    open: async () => ({writeFile: async () => {throw diskFull;}, sync: async () => {}, close: async () => {closes++; throw new Error('close failed');}}),
    unlink: async () => {deletes++; throw new Error('unlink failed');}};
  await assert.rejects(checkDirectory('C:/Audio', {io}), error => error === diskFull);
  assert.equal(closes, 1); assert.equal(deletes, 1);
});
test('samples-folder failures explain full drives, disconnected drives and denied writing without exposing paths', () => {
  const {folderError} = require('../electron/sample-location.cjs');
  for (const [code, text] of [['ENOSPC', /full/], ['EDQUOT', /quota/], ['ENOENT', /no longer available/], ['ENODEV', /Reconnect/], ['EACCES', /permissions/], ['EROFS', /does not allow writing/]])
    assert.match(folderError({code, message: 'C:/private'}, 'fallback'), text);
  assert.equal(folderError({code: 'OTHER'}, 'fallback'), 'fallback');
});
