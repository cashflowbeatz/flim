'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const {writeSettings} = require('../electron/atomic-settings.cjs');
const {singleFlight} = require('../electron/single-flight.cjs');

async function fixture(t) {
  const base = path.resolve(__dirname, '.tmp'); await fs.mkdir(base, {recursive: true});
  const directory = await fs.mkdtemp(path.join(base, 'atomic-'));
  t.after(async () => {assert.ok(directory.startsWith(base + path.sep)); await fs.rm(directory, {recursive: true, force: true});});
  const file = path.join(directory, 'preferences.json'); await fs.writeFile(file, 'original');
  return {directory, file};
}

test('atomic settings reserve a unique file and leave a foreign predictable scratch file intact', async t => {
  const {file, directory} = await fixture(t); await fs.writeFile(file + '.tmp', 'foreign data');
  const flags = [], order = [];
  await writeSettings({...fs, open: async (...args) => {
    flags.push(args); const handle = await fs.open(...args);
    return {writeFile: (...args) => handle.writeFile(...args), sync: async () => {order.push('sync'); await handle.sync();}, close: async () => {order.push('close'); await handle.close();}};
  }, rename: async (...args) => {order.push('replace'); await fs.rename(...args);}}, file, 'new settings');
  assert.equal(flags[0][1], 'wx'); assert.notEqual(flags[0][0], file + '.tmp'); assert.deepEqual(order, ['sync', 'close', 'replace']);
  assert.equal(await fs.readFile(file + '.tmp', 'utf8'), 'foreign data'); assert.equal(await fs.readFile(file, 'utf8'), 'new settings');
  assert.deepEqual((await fs.readdir(directory)).sort(), ['preferences.json', 'preferences.json.tmp']);
});

test('failed settings flush never replaces saved preferences and removes only its owned scratch file', async t => {
  const {file, directory} = await fixture(t);
  await assert.rejects(writeSettings({...fs, open: async (...args) => {
    const handle = await fs.open(...args);
    return {writeFile: (...args) => handle.writeFile(...args), sync: async () => {throw Object.assign(new Error('full'), {code: 'ENOSPC'});}, close: () => handle.close()};
  }}, file, 'discard'), {code: 'ENOSPC'});
  assert.equal(await fs.readFile(file, 'utf8'), 'original'); assert.deepEqual(await fs.readdir(directory), ['preferences.json']);
});

test('settings replacement retries brief Windows sharing failures and bounds permanent failures', async t => {
  const {file, directory} = await fixture(t); let attempts = 0;
  await writeSettings({...fs, rename: async (...args) => {if (++attempts < 3) throw Object.assign(new Error('scanner lock'), {code: 'EPERM'}); return fs.rename(...args);}}, file, 'saved');
  assert.equal(attempts, 3); assert.equal(await fs.readFile(file, 'utf8'), 'saved');
  attempts = 0;
  await assert.rejects(writeSettings({...fs, rename: async () => {attempts++; throw Object.assign(new Error('locked'), {code: 'EBUSY'});}}, file, 'discard'), {code: 'EBUSY'});
  assert.equal(attempts, 3); assert.equal(await fs.readFile(file, 'utf8'), 'saved'); assert.deepEqual(await fs.readdir(directory), ['preferences.json']);
});

test('failed exclusive scratch creation cannot delete a file it never reserved', async t => {
  const {file} = await fixture(t); let deletes = 0;
  await assert.rejects(writeSettings({...fs, open: async () => {throw Object.assign(new Error('exists'), {code: 'EEXIST'});}, unlink: async () => {deletes++;}}, file, 'discard'), {code: 'EEXIST'});
  assert.equal(deletes, 0); assert.equal(await fs.readFile(file, 'utf8'), 'original');
});

test('single-flight folder opening coalesces callers and releases both successful and failed operations', async () => {
  let calls = 0, release, fail = false;
  const open = singleFlight(async () => {calls++; await new Promise(resolve => {release = resolve;}); if (fail) throw new Error('open failed'); return {opened: true};});
  const a = open(), b = open(); assert.equal(a, b); await Promise.resolve(); assert.equal(calls, 1); release(); await a;
  fail = true; const c = open(); const failed = assert.rejects(c, /open failed/); await Promise.resolve(); release(); await failed;
  fail = false; const d = open(); await Promise.resolve(); release(); assert.deepEqual(await d, {opened: true}); assert.equal(calls, 3);
});
