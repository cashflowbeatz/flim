'use strict';

// Opt-in live test: opens the installed FL profile and connects to a real relay room.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {DesktopBridge} = require('../electron/bridge-core.cjs');
const {createDiagnostics} = require('../electron/diagnostics.cjs');
const {version} = require('../package.json');
const {randomUUID} = require('node:crypto');

const major = process.argv[2];
const role = process.argv[3] || 'host';
const pluginProbe = process.argv[4] === 'plugins';
assert.ok(['24', '25', '26'].includes(major), 'Pass 24, 25 or 26 explicitly. This opens FL and contacts the live relay.');
assert.ok(['host', 'member'].includes(role));
assert.ok(!process.argv[4] || pluginProbe && role === 'member', 'Plugin probing requires a fixture host and a member.');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const processes = () => JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-Command',
  'ConvertTo-Json -Compress -InputObject @(Get-Process FL64 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)'], {encoding: 'utf8', windowsHide: true}));

function missingPreset() {
  const variable = n => { const bytes = []; do { bytes.push((n & 127) | (n > 127 ? 128 : 0)); n >>>= 7; } while (n); return Buffer.from(bytes); };
  const event = (id, data) => Buffer.concat([Buffer.from([id]), variable(data.length), data]);
  const record = (tag, data) => { const head = Buffer.alloc(12); head.writeUInt32LE(tag); head.writeBigUInt64LE(BigInt(data.length), 4); return Buffer.concat([head, data]); };
  const wrapper = Buffer.alloc(4); wrapper.writeUInt32LE(8);
  const state = Buffer.concat([wrapper, record(50, Buffer.alloc(16)), record(52, Buffer.from(randomUUID().replaceAll('-', ''), 'hex')),
    record(54, Buffer.from('flim missing-plugin test')), record(55, Buffer.from('M:\\flim-test-missing\\NeverInstalled.vst3'))]);
  const events = Buffer.concat([event(201, Buffer.from('Fruity Wrapper\0', 'utf16le')), event(213, state)]);
  const header = Buffer.alloc(22); header.write('FLhd'); header.writeUInt32LE(6, 4); header.write('FLdt', 14); header.writeUInt32LE(events.length, 18);
  return Buffer.concat([header, events]).toString('base64');
}

async function installedPreset(file) {
  const image = await fs.readFile(file);
  assert.ok(image.length < 256 * 1024 && image.subarray(0, 4).toString() === 'FLhd');
  const start = 8 + image.readUInt32LE(4) + 8, end = start + image.readUInt32LE(start - 4);
  const events = [];
  const variable = n => { const out = []; do { out.push((n & 127) | (n > 127 ? 128 : 0)); n >>>= 7; } while (n); return Buffer.from(out); };
  let changed = 0;
  for (let at = start; at < end;) {
    const first = at, id = image[at++];
    assert.notEqual(id, 0xac, 'This optional fixture expects a legacy scan preset.');
    let n = 1 << (id >> 6);
    if (id >= 192) {
      n = 0; let shift = 0, b;
      do { b = image[at++]; n += (b & 127) * 2 ** shift; shift += 7; assert.ok(shift <= 35); } while (b & 128);
    }
    assert.ok(at + n <= end);
    if (id === 213) {
      const payload = image.subarray(at, at + n), records = [payload.subarray(0, 4)];
      for (let pos = 4; pos < payload.length;) {
        const tag = payload.readUInt32LE(pos), len = Number(payload.readBigUInt64LE(pos + 4));
        assert.ok(Number.isSafeInteger(len) && pos + 12 + len <= payload.length);
        if (tag === 55) {
          const path = Buffer.from('M:\\Different computer\\Older plugin version.vst3'), header = Buffer.alloc(12);
          header.writeUInt32LE(55); header.writeBigUInt64LE(BigInt(path.length), 4);
          records.push(header, path); changed++;
        } else records.push(payload.subarray(pos, pos + 12 + len));
        pos += 12 + len;
      }
      const data = Buffer.concat(records); events.push(Buffer.from([213]), variable(data.length), data);
    } else events.push(image.subarray(first, at + n));
    at += n;
  }
  assert.equal(changed, 1);
  const data = Buffer.concat(events), header = Buffer.from(image.subarray(0, start));
  header.writeUInt32LE(data.length, start - 4);
  return Buffer.concat([header, data]).toString('base64');
}

(async () => {
  assert.equal(processes().length, 0, 'Close other FL Studio processes before this live startup test.');
  const root = path.resolve(__dirname, '.tmp');
  await fs.mkdir(root, {recursive: true});
  const directory = await fs.mkdtemp(path.join(root, `native-room-${major}-`));
  const diagnostics = await createDiagnostics({rootDir: path.join(directory, 'logs'), appVersion: version});
  assert.ok(diagnostics.available);
  let pid, fixture, fixtureBuild;
  let socket;
  const bridge = new DesktopBridge({
    nativeDir: process.env.FLIM_TEST_NATIVE_DIR ? path.resolve(process.env.FLIM_TEST_NATIVE_DIR) : path.resolve(__dirname, '../../out-experimental'),
    preferencesFile: path.join(directory, 'preferences.json'),
    nativeLogDir: diagnostics.nativeDirectory,
    record: (event, fields) => {
      if (event === 'launch_confirmed') pid = fields.pid;
      return diagnostics.record(event, fields);
    },
  });
  let result;
  try {
    const state = await bridge.initialize();
    const install = state.installs.find(item => item.version.startsWith(`${major}.`));
    assert.ok(install?.supported, `No supported installed FL${major} found.`);
    const input = {name: `Startup FL${major}`, installId: install.id};
    if (role === 'member') {
      fixture = new DesktopBridge({nativeDir: bridge.nativeDir, preferencesFile: path.join(directory, 'fixture.json')});
      const fixtureState = await fixture.initialize();
      const fixtureInstall = fixtureState.installs.find(item => item.version === (process.env.FLIM_TEST_HOST_FL || install.version));
      assert.ok(fixtureInstall?.supported);
      fixtureBuild = fixtureInstall.version;
      await fixture.createRoom({name: 'Relay fixture', installId: fixtureInstall.id});
      const reserved = fixture.room;
      socket = new WebSocket(`wss://flim-relay.cashflowbeatz.workers.dev${'/room/'}${reserved.code}`);
      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('Relay fixture timed out.')), 12_000);
        socket.addEventListener('open', () => socket.send(JSON.stringify({t: 'host', password: reserved.password, owner: reserved.hostToken,
          user: {id: randomUUID(), name: 'Relay fixture'}, transport: 2, compat: {protocol: 1, fl: fixtureBuild, wire: 'flim-25-26-v1'}})));
        socket.addEventListener('message', event => {
          const message = JSON.parse(event.data);
          if (message.t === 'welcome') { clearTimeout(timer); resolve(); }
          if (message.t === 'error') { clearTimeout(timer); reject(new Error(`Relay fixture refused: ${message.code}`)); }
        });
        socket.addEventListener('error', () => { clearTimeout(timer); reject(new Error('Relay fixture socket failed.')); });
      });
      // The fixture sends no project data: this verifies admission, not project loading.
      await bridge.joinRoom({...input, code: reserved.code});
    } else await bridge.createRoom(input);
    const started = Date.now();
    await bridge.launchFL();
    assert.ok(Number.isSafeInteger(pid));
    let log = '', connected = false;
    while (Date.now() - started < 80_000) {
      try { log = await fs.readFile(path.join(diagnostics.nativeDirectory, `native-${pid}.log`), 'utf8'); } catch {}
      assert.ok(!/STARTUP .*failed|readiness timed out|SHARING stopped|NET relay error/.test(log), 'Native startup/connection failure; inspect test diagnostics.');
      if (log.includes('NET welcome accepted')) {
        const current = await bridge.refreshRoom();
        connected = current.room?.status === 'active' && current.room.peers.some(peer => peer.id.startsWith(`fl-${pid}-`) && peer.role === role);
        if (connected) break;
      }
      assert.ok(processes().includes(pid), 'The launched FL Studio process exited before connecting.');
      await pause(1000);
    }
    assert.ok(connected, 'FL did not connect to the room within 80 seconds.');
    if (pluginProbe) {
      const logPath = path.join(diagnostics.nativeDirectory, `native-${pid}.log`);
      const waitFor = async (needle) => {
        const until = Date.now() + 30_000;
        while (Date.now() < until) {
          log = await fs.readFile(logPath, 'utf8');
          assert.ok(!/SHARING stopped|NET relay error|LOAD faulted/.test(log), 'Plugin probe failed; inspect native diagnostics.');
          if (log.includes(needle)) return;
          await pause(250);
        }
        assert.fail(`Native plugin probe timed out: ${needle}`);
      };
      const data = missingPreset();
      const preset = {t: 'op', op: {kind: 'plug.state', unit: 'fx', trk: 1, slot: 9, data}};
      socket.send(JSON.stringify(preset));
      await waitFor('PLUGIN unavailable unit=fx 1/9');
      assert.match(log, /matching ID not present/); // Proves the fake VST never reached FL's loader.
      socket.send(JSON.stringify(preset)); // Repeated identical state must not produce a load loop.
      const pluginId = (0x70000000 + ((1 * 64 + 9) * 65536) + 0x8000) >>> 0;
      socket.send(JSON.stringify({t: 'op', op: {kind: 'param.set', id: pluginId, v: 123}}));
      const panId = 0x70000000 + (1 << 22) + 0x1fc1;
      socket.send(JSON.stringify({t: 'op', op: {kind: 'param.set', id: panId, v: 0}}));
      await waitFor(`APPLY id=0x${panId.toString(16)} v=0`);
      assert.ok(!log.includes(`APPLY id=0x${pluginId.toString(16)}`), 'Unavailable plugin parameters reached FL.');
      assert.equal(log.split('PLUGIN unavailable unit=fx 1/9').length - 1, 1);
      const current = await bridge.refreshRoom();
      assert.equal(current.room.status, 'active');
      assert.ok(current.room.peers.some(peer => peer.id.startsWith(`fl-${pid}-`)));
      socket.send(JSON.stringify({t: 'op', op: {kind: 'fx.clear', trk: 1, slot: 9}}));
      await waitFor('PLUGIN unavailable state discarded for empty fx 1/9');
      if (process.env.FLIM_TEST_VST_PRESET) {
        const data = await installedPreset(process.env.FLIM_TEST_VST_PRESET);
        socket.send(JSON.stringify({t: 'op', op: {kind: 'plug.state', unit: 'fx', trk: 1, slot: 9, data}}));
        await waitFor('PLUGIN local VST3 path selected by stable ID');
        await waitFor('PLUG applied unit=fx 1/9 ret=0');
        socket.send(JSON.stringify({t: 'op', op: {kind: 'fx.clear', trk: 1, slot: 9}}));
        await waitFor('PLUG cleared fx 1/9 (remote) ret=0');
      }
    }
    result = {flVersion: install.version, pid, role, connected: true, messageHooksReady: log.includes('message hooks ready'), welcomeAccepted: true,
      startupMs: Date.now() - started, fixtureBuild, pluginProbe, installedVstProbe: Boolean(process.env.FLIM_TEST_VST_PRESET), nativeDirectory: diagnostics.nativeDirectory};
  } finally {
    // Close only this test's FL process normally; do not force termination or save projects.
    let cleanupError;
    try {
      if (Number.isSafeInteger(pid)) {
        execFileSync('powershell.exe', ['-NoProfile', '-Command',
          `$ownedFl = Get-Process -Id ${pid} -ErrorAction SilentlyContinue; if ($ownedFl -and $ownedFl.ProcessName -eq 'FL64') { [void]$ownedFl.CloseMainWindow() }`], {windowsHide: true});
        // A real installed VST may mark the untitled fixture modified and prompt to save.
        for (let attempt = 0; attempt < 60 && processes().includes(pid); attempt++) await pause(500);
        assert.ok(!processes().includes(pid), 'The test FL process needs manual closing; it was not force-terminated.');
      }
    } catch (error) { cleanupError = error; }
    socket?.close();
    const cleanup = await Promise.allSettled([bridge.leaveRoom(), fixture?.leaveRoom()]);
    for (const item of cleanup) {
      if (item.status === 'rejected' && !cleanupError) cleanupError = item.reason;
    }
    await diagnostics.flush();
    if (cleanupError) throw cleanupError;
  }
  await fs.writeFile(path.join(directory, 'result.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result));
})().catch(error => { console.error(error.message); process.exitCode = 1; });
