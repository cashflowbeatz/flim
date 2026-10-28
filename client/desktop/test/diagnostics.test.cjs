'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const { createDiagnostics } = require('../electron/diagnostics.cjs');

async function fixture(t) {
  const parent = path.resolve(__dirname, '.tmp');
  await fs.mkdir(parent, { recursive: true });
  const directory = await fs.mkdtemp(path.join(parent, 'diagnostics-'));
  t.after(async () => {
    const relative = path.relative(parent, directory);
    assert.ok(relative && !relative.startsWith('..') && !path.isAbsolute(relative));
    await fs.rm(directory, { recursive: true, force: true });
  });
  return directory;
}

async function records(directory) {
  const names = (await fs.readdir(directory)).filter(name => /^events(?:\.[12])?\.jsonl$/.test(name)).sort().reverse();
  return (await Promise.all(names.map(name => fs.readFile(path.join(directory, name), 'utf8'))))
    .flatMap(text => text.trim().split('\n').filter(Boolean).map(line => JSON.parse(line)));
}

test('diagnostics record only allowed fields and redact credentials, room links, paths and codes', async t => {
  const rootDir = await fixture(t);
  const log = await createDiagnostics({ rootDir, appVersion: '0.2.6' });
  assert.equal(log.available, true);
  assert.equal(log.nativeDirectory, log.directory);
  assert.equal(path.dirname(log.directory), await fs.realpath(rootDir));
  const secret = 'secret-unquoted-value';
  await log.record('request_failed', {
    operation: '/rooms/status', status: 429, code: 'room_rate_limit', durationMs: 123,
    room: '12345678', name: 'Do not store me', password: secret,
    environment: { SECRET: secret }, body: { password: secret }, url: 'https://example.invalid/private',
    message: `password=${secret}; hostToken="a secret with spaces"; blob:${'a'.repeat(64)}; x-flim-key=header-secret; ` +
      'https://example.invalid/room/12345678?password=another-secret room 1234 5678 name="A Person"',
    stack: 'Error at C:\\Users\\Private Person\\Documents\\secret.js:42:1)\n at /home/private/file.js:1:2',
    engineHash: 'b'.repeat(64), unknown: 'discard', peerCount: Infinity,
  });
  await log.record('not_a_supported_event', { message: 'discard unknown event' });
  await log.flush();
  const text = await fs.readFile(path.join(log.directory, 'events.jsonl'), 'utf8');
  for (const value of [secret, 'a secret with spaces', 'a'.repeat(64), 'header-secret', 'example.invalid', '12345678', '1234 5678',
    'A Person', 'Private Person', '/home/private', 'Do not store me', 'another-secret', 'discard unknown event']) assert.ok(!text.includes(value), value);
  const [entry] = await records(log.directory);
  assert.equal(entry.operation, '/rooms/status'); assert.equal(entry.status, 429); assert.equal(entry.durationMs, 123);
  assert.equal(entry.engineHash, 'b'.repeat(64));
  for (const key of ['room', 'name', 'password', 'environment', 'body', 'url', 'unknown', 'peerCount']) assert.equal(Object.hasOwn(entry, key), false);
  assert.equal((await records(log.directory)).length, 1);
});

test('diagnostic files rotate at 2 MiB and retain exactly three bounded event files', async t => {
  const log = await createDiagnostics({ rootDir: await fixture(t), appVersion: '0.2.6' });
  for (let i = 0; i < 2800; i++) {
    await log.record('process_error', { message: `entry-${i} ` + 'detail '.repeat(120), stack: 'frame '.repeat(400) });
    if (i % 64 === 0) await log.flush();
  }
  await log.flush();
  const names = (await fs.readdir(log.directory)).filter(name => name.startsWith('events'));
  assert.deepEqual(names.sort(), ['events.1.jsonl', 'events.2.jsonl', 'events.jsonl']);
  for (const name of names) assert.ok((await fs.stat(path.join(log.directory, name))).size <= 2 * 1024 * 1024);
  const rows = await records(log.directory);
  assert.ok(rows.some(row => row.message.startsWith('entry-2799 ')));
  assert.ok(!rows.some(row => row.message.startsWith('entry-0 ')));
  assert.equal(log.available, true);
});

test('stalled writes bound the pending queue and report dropped diagnostic events', async t => {
  const rootDir = await fixture(t);
  let release, entered;
  const enteredPromise = new Promise(resolve => { entered = resolve; });
  const held = new Promise(resolve => { release = resolve; });
  let first = true;
  const io = { ...fs, async appendFile(...args) { if (first) { first = false; entered(); await held; } return fs.appendFile(...args); } };
  const log = await createDiagnostics({ rootDir, appVersion: '0.2.6', io });
  await log.record('app_start', { appVersion: '0.2.6' });
  await enteredPromise;
  for (let i = 0; i < 2000; i++) await log.record('process_error', { message: 'detail '.repeat(120), stack: 'frame '.repeat(400) });
  release(); await log.flush();
  const rows = await records(log.directory), dropped = rows.find(row => row.event === 'events_dropped');
  assert.ok(rows.length <= 258);
  assert.ok(dropped && dropped.count > 0);
  assert.equal(rows.filter(row => row.event === 'process_error').length + dropped.count, 2000);
  await log.record('app_exit'); await log.flush();
  assert.equal((await records(log.directory)).at(-1).event, 'app_exit');
});

test('creation and disk-write failures never reject or block the caller', async t => {
  const rootDir = await fixture(t);
  const creation = await createDiagnostics({ rootDir, appVersion: '0.2.6', io: { ...fs, async mkdir() { throw new Error('denied'); } } });
  assert.equal(creation.available, false); assert.equal(creation.nativeDirectory, '');
  await creation.record('app_start'); await creation.flush();
  const log = await createDiagnostics({ rootDir, appVersion: '0.2.6', io: { ...fs, async appendFile() { throw new Error('disk full'); } } });
  await log.record('app_start'); await log.record('app_exit'); await log.flush();
  assert.equal(log.available, false); assert.equal(log.nativeDirectory, '');
  await log.record('process_error', { message: 'still harmless' }); await log.flush();
});

async function oldSession(root, age, options = {}) {
  const createdAt = Date.now() - age, date = new Date(createdAt);
  const name = date.toISOString().replace(/[:.]/g, '-') + '_' + randomUUID(), directory = path.join(root, name);
  await fs.mkdir(directory);
  const metadata = { format: 'flim-diagnostics-v1', directory: name, createdAt, appPid: 0x7fffffff, nativePids: [], ...options.metadata };
  await fs.writeFile(path.join(directory, 'session.json'), JSON.stringify(metadata));
  await fs.writeFile(path.join(directory, 'events.jsonl'), '{}\n');
  for (const name of options.files ?? []) await fs.writeFile(path.join(directory, name), 'preserve me');
  for (const name of await fs.readdir(directory)) await fs.utimes(path.join(directory, name), date, date);
  await fs.utimes(directory, date, date);
  return directory;
}

test('retention prunes only old inactive owned runs and preserves active/native/unknown paths', async t => {
  const rootDir = await fixture(t), day = 86400_000;
  const obsolete = [];
  for (let i = 0; i < 12; i++) obsolete.push(await oldSession(rootDir, (30 + i) * day));
  const appActive = await oldSession(rootDir, 40 * day, { metadata: { appPid: process.pid } });
  const nativeActive = await oldSession(rootDir, 40 * day, { metadata: { nativePids: [process.pid] }, files: ['flim_engine.log'] });
  const nativeUnknown = await oldSession(rootDir, 40 * day, { files: ['flim_engine.log'] });
  const otherFile = await oldSession(rootDir, 40 * day, { files: ['my-project.flp'] });
  const recent = await oldSession(rootDir, day);
  const arbitrary = path.join(rootDir, 'unrelated'); await fs.mkdir(arbitrary); await fs.writeFile(path.join(arbitrary, 'important'), 'safe');
  const log = await createDiagnostics({ rootDir, appVersion: '0.2.6' });
  assert.equal(log.available, true);
  for (const directory of [appActive, nativeActive, nativeUnknown, otherFile, recent, arbitrary]) assert.equal((await fs.stat(directory)).isDirectory(), true);
  assert.equal((await fs.readdir(rootDir)).filter(name => name !== 'unrelated').length, 10);
  assert.ok((await Promise.all(obsolete.map(directory => fs.stat(directory).then(() => true, () => false)))).some(exists => !exists));
});

test('retention skips symbolic links and does not traverse linked folders', async t => {
  const fixtureRoot = await fixture(t), rootDir = path.join(fixtureRoot, 'logs'), outside = path.join(fixtureRoot, 'outside');
  await fs.mkdir(rootDir); await fs.mkdir(outside); await fs.writeFile(path.join(outside, 'important'), 'safe');
  for (let i = 0; i < 11; i++) await oldSession(rootDir, (20 + i) * 86400_000);
  const linked = await oldSession(rootDir, 50 * 86400_000);
  await fs.symlink(outside, path.join(linked, 'linked-folder'), process.platform === 'win32' ? 'junction' : 'dir');
  const date = new Date(Date.now() - 50 * 86400_000); await fs.utimes(linked, date, date);
  await createDiagnostics({ rootDir, appVersion: '0.2.6' });
  assert.equal(await fs.readFile(path.join(outside, 'important'), 'utf8'), 'safe');
  assert.equal((await fs.lstat(path.join(linked, 'linked-folder'))).isSymbolicLink(), true);
  const linkedRoot = path.join(fixtureRoot, 'linked-root');
  await fs.symlink(outside, linkedRoot, process.platform === 'win32' ? 'junction' : 'dir');
  assert.equal((await createDiagnostics({ rootDir: linkedRoot, appVersion: '0.2.6' })).available, false);
  assert.deepEqual(await fs.readdir(outside), ['important']);
});

test('confirmed native PIDs are persisted so a later desktop run can retain active FL logs', async t => {
  const log = await createDiagnostics({ rootDir: await fixture(t), appVersion: '0.2.6' });
  const pid = process.pid + 1;
  await log.record('launch_confirmed', { pid, flVersion: '26.1.7.5654', role: 'host' });
  await log.record('launch_confirmed', { pid, flVersion: '26.1.7.5654' });
  await log.flush();
  const metadata = JSON.parse(await fs.readFile(path.join(log.directory, 'session.json'), 'utf8'));
  assert.deepEqual(metadata.nativePids, [pid]); assert.equal(metadata.appPid, process.pid);
});

test('native PID log rotations are pruned only when both saved and filename PIDs are inactive', async t => {
  const rootDir = await fixture(t), age = 20 * 86400_000;
  for (let i = 0; i < 11; i++) await oldSession(rootDir, age + i);
  const inactive = await oldSession(rootDir, 40 * 86400_000, {
    metadata: { nativePids: [0x7fffffff] }, files: ['native-2147483647.log', 'native-2147483647.log.1', 'native-2147483647.log.2'],
  });
  const active = await oldSession(rootDir, 50 * 86400_000, {
    metadata: { nativePids: [0x7fffffff] }, files: [`native-${process.pid}.log`],
  });
  await createDiagnostics({ rootDir, appVersion: '0.2.6' });
  await assert.rejects(fs.stat(inactive), { code: 'ENOENT' });
  assert.equal((await fs.stat(active)).isDirectory(), true);
});

test('startup, native fingerprint and child-process evidence keeps only its allowed metadata', async t => {
  const log = await createDiagnostics({ rootDir: await fixture(t), appVersion: '0.2.6' });
  await log.record('native_files', { launcherHash: 'c'.repeat(64), engineHash: 'd'.repeat(64), path: 'private' });
  await log.record('renderer_load_failed', { code: -6, message: 'Could not load the application.' });
  await log.record('child_exit', { reason: 'crashed', exitCode: 1 });
  await log.flush();
  const rows = await records(log.directory);
  assert.equal(rows[0].launcherHash, 'c'.repeat(64)); assert.equal(rows[0].engineHash, 'd'.repeat(64));
  assert.equal(Object.hasOwn(rows[0], 'path'), false);
  assert.equal(rows[1].code, -6); assert.equal(rows[2].reason, 'crashed');
});

test('out-of-range clocks cannot disable local diagnostic startup or event dates', async t => {
  for (const timestamp of [1e300, -1, Infinity, NaN, 253402300800000]) {
    const log = await createDiagnostics({rootDir: await fixture(t), appVersion: '0.2.20', now: () => timestamp});
    assert.equal(log.available, true);
    await log.record('app_start'); await log.flush();
    const [entry] = await records(log.directory);
    assert.ok(Number.isFinite(Date.parse(entry.at)));
    assert.match(path.basename(log.directory), /^\d{4}-/);
  }
});

test('diagnostic text respects byte limits without broken multibyte characters', async t => {
  const log = await createDiagnostics({rootDir: await fixture(t), appVersion: '0.2.20'});
  await log.record('process_error', {message: 'é '.repeat(1000), reason: '𝄞 '.repeat(1000), stack: '界 '.repeat(2000)});
  await log.flush();
  const [entry] = await records(log.directory);
  for (const [field, limit] of [['message', 768], ['reason', 256], ['stack', 2048]]) {
    assert.ok(Buffer.byteLength(entry[field]) <= limit, field);
    assert.ok(!entry[field].includes('\ufffd'), field); assert.ok(entry[field].endsWith('…'));
  }
});

test('failed PID metadata replacement keeps the last valid metadata and a retention stop marker', async t => {
  const log = await createDiagnostics({rootDir: await fixture(t), appVersion: '0.2.20', io: {...fs, rename: async (...args) => {
    if (path.basename(args[0]) === 'session.pending') throw new Error('replacement denied');
    return fs.rename(...args);
  }}});
  await log.record('launch_confirmed', {pid: process.pid + 1}); await log.flush();
  const original = JSON.parse(await fs.readFile(path.join(log.directory, 'session.json'), 'utf8'));
  const pending = JSON.parse(await fs.readFile(path.join(log.directory, 'session.pending'), 'utf8'));
  assert.deepEqual(original.nativePids, []); assert.deepEqual(pending.nativePids, [process.pid + 1]);
  assert.equal(log.available, false);
});

test('deliberate room-status cancellation is a supported redacted diagnostic event', async t => {
  const log = await createDiagnostics({rootDir: await fixture(t), appVersion: '0.2.20'});
  await log.record('request_canceled', {operation: '/rooms/status', durationMs: 25, room: '12345678'}); await log.flush();
  assert.deepEqual((await records(log.directory)).map(({at, ...entry}) => entry), [{event: 'request_canceled', operation: '/rooms/status', durationMs: 25}]);
});
