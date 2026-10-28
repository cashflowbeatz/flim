'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const {EventEmitter} = require('node:events');
const {PeerFiles} = require('../electron/peer-core.cjs');
const HASH = 'a'.repeat(64);
async function fixture(t) {
  const root = path.join(__dirname, '.tmp'); await fs.mkdir(root, {recursive: true});
  const dir = await fs.mkdtemp(path.join(root, 'peer-unit-')), commands = [], sockets = [];
  let send = () => {}, chunks = async () => {};
  const core = new PeerFiles({relay: 'https://relay.invalid', transport: {send: m => {commands.push(m); send(m);}, chunk: m => chunks(m)}, alive: pid => pid === 123,
    connect: () => {const ws = new EventEmitter(); ws.readyState = 1; ws.send = () => {}; ws.close = () => {}; sockets.push(ws); return ws;}});
  t.after(async () => {await core.close(); assert.ok(dir.startsWith(root + path.sep)); await fs.rm(dir, {recursive: true, force: true});});
  const client = await core.prepare('12345678'), s = [...core.sessions.values()][0];
  const call = (route, data, headers = {}) => fetch(client.env.FLIM_PEER_URL + route, {method: data === undefined ? 'GET' : 'POST', headers: {'x-flim-key': client.env.FLIM_PEER_TOKEN, ...headers}, ...(data === undefined ? {} : {body: JSON.stringify(data)})});
  const connect = async generation => assert.equal((await call('/connect', {generation, token: 'b'.repeat(64)})).status, 200);
  const offer = async () => {const file = path.join(dir, 'kick.wav'); await fs.writeFile(file, 'sample'); return {generation: s.generation, h: HASH, s: 6, p: Buffer.from(file, 'utf16le').toString('base64')};};
  return {core, client, s, call, connect, offer, commands, sockets, setSend: fn => {send = fn;}, setChunks: fn => {chunks = fn;}};
}
test('broker rejects browser origins, wrong capabilities and malformed native generations', async t => {
  const f = await fixture(t);
  assert.equal((await f.call('/connect', {}, {'x-flim-key': 'wrong'})).status, 403);
  assert.equal((await f.call('/connect', {}, {origin: 'https://foreign.invalid'})).status, 403);
  for (const generation of [0, -1, 1.1, '1']) assert.equal((await f.call('/connect', {generation, token: 'b'.repeat(64)})).status, 409);
  await f.connect(1); assert.equal((await f.call('/connect', {generation: 1, token: 'b'.repeat(64)})).status, 409);
});
test('parallel preparations cannot exceed eight launch sessions and closed brokers cannot reopen', async t => {
  const f = await fixture(t);
  const results = await Promise.allSettled(Array.from({length: 12}, () => f.core.prepare('12345678')));
  assert.equal(results.filter(r => r.status === 'fulfilled').length, 7); assert.equal(f.core.sessions.size, 8);
  await f.core.close(); await assert.rejects(f.core.prepare('12345678')); assert.equal(f.core.server, null);
});
test('source registration rejects relative paths, noncanonical base64, size mismatches and stale sessions', async t => {
  const f = await fixture(t); await f.connect(1); const data = await f.offer();
  for (const bad of [{...data, p: Buffer.from('relative.wav', 'utf16le').toString('base64')}, {...data, p: data.p + '\n'}, {...data, s: 5}, {...data, generation: 0}]) assert.notEqual((await f.call('/offer', bad)).status, 200);
  assert.equal((await f.call('/offer', data)).status, 200); await f.connect(2); assert.equal(f.s.offers.size, 0);
});
test('serving reserves concurrency before opening files and releases it after cancellation', async t => {
  const f = await fixture(t); await f.connect(1); await f.call('/offer', await f.offer());
  const pending = []; f.setChunks(() => new Promise(resolve => pending.push(resolve)));
  const serve = id => f.core.event({type: 'serve', session: f.s.id, id, hash: HASH, bytes: 6});
  const results = await Promise.all([serve('a'), serve('b'), serve('c')]); assert.deepEqual(results, [true, true, false]);
  assert.equal(f.s.outgoing, 2);
  while (pending.length < 2) await new Promise(r => setTimeout(r, 5));
  f.client.cancel(); pending.forEach(resolve => resolve());
  for (let i = 0; f.s.outgoing && i < 100; i++) await new Promise(r => setTimeout(r, 5));
  assert.equal(f.s.outgoing, 0);
});
test('changed or deleted registered sources decline direct serving without leaking slots', async t => {
  const f = await fixture(t); await f.connect(1); const data = await f.offer(); await f.call('/offer', data);
  const file = Buffer.from(data.p, 'base64').toString('utf16le'); await fs.appendFile(file, '!');
  assert.equal(await f.core.event({type: 'serve', session: f.s.id, id: 'a', hash: HASH, bytes: 6}), false);
  await fs.unlink(file); assert.equal(await f.core.event({type: 'serve', session: f.s.id, id: 'b', hash: HASH, bytes: 6}), false); assert.equal(f.s.outgoing, 0);
});
test('direct broker download streams exact bytes and cleans transfer resources', async t => {
  const f = await fixture(t); await f.connect(1); f.s.ready.add('other');
  f.setSend(m => {if (m.type === 'get') void (async () => {
    await f.core.event({type: 'start', session: f.s.id, id: m.id, bytes: 6});
    await f.core.event({type: 'chunk', session: f.s.id, id: m.id, data: Buffer.from('sample')});
    await f.core.event({type: 'end', session: f.s.id, id: m.id});
  })();});
  const response = await f.call(`/blob/${HASH}?bytes=6&generation=1`); assert.equal(response.status, 200); assert.equal(await response.text(), 'sample');
  assert.equal(f.core.transfers.size, 0); assert.equal(f.s.incoming, 0);
});
test('oversized chunks abort a direct download instead of accepting partial content', async t => {
  const f = await fixture(t); await f.connect(1); f.s.ready.add('other');
  f.setSend(m => {if (m.type === 'get') void (async () => {
    await f.core.event({type: 'start', session: f.s.id, id: m.id, bytes: 6});
    await f.core.event({type: 'chunk', session: f.s.id, id: m.id, data: Buffer.alloc(7)});
  })();});
  await assert.rejects(async () => {const response = await f.call(`/blob/${HASH}?bytes=6&generation=1`); await response.arrayBuffer();});
  assert.equal(f.core.transfers.size, 0); assert.equal(f.s.incoming, 0);
});
test('new native generations and signaling loss clear ready peers and pending downloads', async t => {
  const f = await fixture(t); await f.connect(1); f.s.ready.add('other');
  const response = f.call(`/blob/${HASH}?bytes=6&generation=1`);
  while (!f.core.transfers.size) await new Promise(r => setTimeout(r, 5));
  await f.connect(2); assert.equal((await response).status, 404); assert.equal(f.s.ready.size, 0); assert.equal(f.core.transfers.size, 0);
  f.s.ready.add('other'); f.s.ws.emit('close', 4000); assert.equal(f.s.ready.size, 0); assert.equal(f.s.ws, null);
});
test('unavailable peers, invalid generations and incoming capacity return promptly', async t => {
  const f = await fixture(t); await f.connect(1);
  assert.equal((await f.call(`/blob/${HASH}?bytes=6&generation=1`)).status, 404);
  assert.equal((await f.call(`/blob/${HASH}?bytes=6&generation=0`)).status, 400);
  f.s.ready.add('other'); f.s.incoming = 2;
  assert.equal((await f.call(`/blob/${HASH}?bytes=6&generation=1`)).status, 404);
});
test('only launched living FL processes keep the companion active', async t => {
  const f = await fixture(t); assert.equal(f.core.active, false); f.client.launched(123); assert.equal(f.core.active, true);
  f.client.launched(456); assert.equal(f.core.active, false); f.client.cancel(); assert.equal(f.core.sessions.size, 0);
});
