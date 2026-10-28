'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const { randomUUID } = require('node:crypto');

const FILE_BYTES = 2 * 1024 * 1024;
const QUEUE_BYTES = 512 * 1024;
const QUEUE_COUNT = 256;
const KEEP_SESSIONS = 10;
const RETENTION_MS = 7 * 24 * 60 * 60 * 1000;
const FORMAT = 'flim-diagnostics-v1';
const SESSION_NAME = /^\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}-\d{3}Z_[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
const NATIVE_FILE = /^(?:native-\d{1,10}\.log(?:\.[12])?|flim_engine(?:\.[12])?\.log(?:\.[12])?)$/;
const KNOWN_FILE = /^(?:session\.json|events(?:\.[12])?\.jsonl|native-\d{1,10}\.log(?:\.[12])?|flim_engine(?:\.[12])?\.log(?:\.[12])?)$/;
const EVENTS = new Set(['app_start', 'install_inspected', 'request_failed', 'launch_started', 'launch_confirmed', 'launch_failed',
  'room_status', 'ipc_error', 'renderer_error', 'renderer_exit', 'process_error', 'app_exit', 'events_dropped',
  'native_files', 'native_files_missing', 'settings_failed', 'room_left', 'renderer_load_failed', 'startup_failed', 'child_exit', 'request_retry', 'request_canceled', 'renderer_recovery', 'sample_folder_changed', 'file_status']);
const FIELDS = new Set(['appVersion', 'pid', 'flVersion', 'supported', 'experimental', 'operation', 'status', 'code', 'message',
  'durationMs', 'role', 'peerCount', 'state', 'stack', 'reason', 'exitCode', 'launcherHash', 'engineHash', 'flEngineHash', 'custom']);

function bounded(value, max) {
  const bytes = Buffer.from(value, 'utf8');
  if (bytes.length <= max) return value;
  let end = Math.max(0, max - 3);
  while (end > 0 && (bytes[end] & 0xc0) === 0x80) end--;
  return bytes.subarray(0, end).toString('utf8') + '…';
}

function redact(value, max) {
  return bounded(value.slice(0, 16_384)
    .replace(/\b(?:https?|wss?):\/\/[^\s<>"')]+/gi, '[redacted-url]')
    .replace(/\b(?:Bearer|Basic)\s+[^\s,;]+/gi, '[redacted-authorization]')
    .replace(/(["']?(?:password|host[_-]?token|blob|key|token|owner|secret|authorization|cookie|room(?:[_-]?code)?|session|name|username|displayName|FLIM_[A-Z_]+|x-flim-[a-z-]+)["']?\s*[:=]\s*)(?:"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[^\s,;}\]]+)/gi, '$1[redacted]')
    .replace(/\b[A-Za-z]:[\\/][^\r\n<>"|)]*/g, '[redacted-path]')
    .replace(/\\\\[^\r\n<>"|)]*/g, '[redacted-path]')
    .replace(/(^|[\s(])\/[^\s<>"')]+/g, '$1[redacted-path]')
    .replace(/\b\d{4}[ -]?\d{4}\b/g, '[redacted-room]')
    .replace(/\b[A-Za-z0-9_-]{32,}\b/g, '[redacted-token]')
    .replace(/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/g, ''), max);
}

function sanitize(fields) {
  const clean = {};
  if (!fields || typeof fields !== 'object') return clean;
  for (const key of FIELDS) {
    const descriptor = Object.getOwnPropertyDescriptor(fields, key);
    if (!descriptor || !Object.hasOwn(descriptor, 'value')) continue;
    const value = descriptor.value;
    if (typeof value === 'boolean' && ['supported', 'experimental', 'custom'].includes(key)) clean[key] = value;
    else if (typeof value === 'number' && Number.isFinite(value) && Math.abs(value) <= Number.MAX_SAFE_INTEGER && ['pid', 'status', 'durationMs', 'peerCount', 'exitCode', 'code'].includes(key)) {
      if (key === 'pid' && (!Number.isSafeInteger(value) || value < 1 || value > 0xffffffff)) continue;
      clean[key] = value;
    } else if (typeof value === 'string') {
      if (key.endsWith('Hash')) { if (/^[a-f0-9]{64}$/i.test(value)) clean[key] = value.toLowerCase(); }
      else if (key === 'message' || key === 'stack' || key === 'reason') clean[key] = redact(value, key === 'stack' ? 2048 : key === 'message' ? 768 : 256);
      else if (key === 'flVersion') { if (/^\d{1,5}(?:\.\d{1,5}){3}$/.test(value)) clean[key] = value; }
      else if (key === 'appVersion') { if (/^\d+\.\d+\.\d+(?:[-.][a-z0-9.-]{1,32})?$/i.test(value) && value.length <= 64) clean[key] = value; }
      else if (['operation', 'status', 'code', 'role', 'state'].includes(key) && /^[a-z][a-z0-9_.\/-]{0,63}$/i.test(value)) clean[key] = redact(value, 64);
      else if (key === 'operation' && /^\/rooms(?:\/(?:join|status|cancel))?$/.test(value)) clean[key] = value;
    }
  }
  return clean;
}

function processAlive(pid) {
  if (!Number.isSafeInteger(pid) || pid < 1) return true;
  try { process.kill(pid, 0); return true; }
  catch (error) { return error.code !== 'ESRCH'; }
}

async function prune(io, root, current, now) {
  const entries = await io.readdir(root, { withFileTypes: true });
  const owned = [];
  for (const entry of entries.slice(0, 256)) {
    if (!SESSION_NAME.test(entry.name) || !entry.isDirectory() || entry.isSymbolicLink()) continue;
    const directory = path.join(root, entry.name);
    try {
      const stat = await io.lstat(directory);
      if (!stat.isDirectory() || stat.isSymbolicLink() || await io.realpath(directory) !== directory) continue;
      const metadataPath = path.join(directory, 'session.json'), metadataStat = await io.lstat(metadataPath);
      if (!metadataStat.isFile() || metadataStat.isSymbolicLink() || metadataStat.size > 8192) continue;
      const metadata = JSON.parse(await io.readFile(metadataPath, 'utf8'));
      if (metadata.format !== FORMAT || metadata.directory !== entry.name || !Number.isFinite(metadata.createdAt) ||
          !Array.isArray(metadata.nativePids) || metadata.nativePids.length > 64) continue;
      owned.push({ directory, metadata, touched: Math.max(stat.mtimeMs, metadataStat.mtimeMs) });
    } catch {}
  }
  owned.sort((a, b) => a.metadata.createdAt - b.metadata.createdAt);
  let remaining = owned.length;
  for (const item of owned) {
    if (remaining <= KEEP_SESSIONS) break;
    const { directory, metadata } = item;
    if (directory === current || Math.max(metadata.createdAt, item.touched) > now - RETENTION_MS ||
        metadata.unknownNativeProcesses || processAlive(metadata.appPid) || metadata.nativePids.some(processAlive)) continue;
    try {
      const files = await io.readdir(directory, { withFileTypes: true });
      if (files.length > 128 || files.some(entry => !KNOWN_FILE.test(entry.name) || !entry.isFile() || entry.isSymbolicLink())) continue;
      if (files.some(entry => NATIVE_FILE.test(entry.name)) && !metadata.nativePids.length) continue;
      if (files.some(entry => /^native-\d+\./.test(entry.name) && processAlive(Number(entry.name.match(/^native-(\d+)/)[1])))) continue;
      const details = await Promise.all(files.map(async entry => ({ file: path.join(directory, entry.name), stat: await io.lstat(path.join(directory, entry.name)) })));
      if (details.some(({ stat }) => !stat.isFile() || stat.isSymbolicLink() || stat.mtimeMs > now - RETENTION_MS)) continue;
      // Remove only known regular files, never recurse into a directory or follow a link.
      if ((await io.lstat(directory)).isSymbolicLink() || await io.realpath(directory) !== directory) continue;
      for (const { file } of details) await io.unlink(file);
      await io.rmdir(directory);
      remaining--;
    } catch {}
  }
}

async function createDiagnostics({ rootDir, appVersion, now = Date.now, io = fs } = {}) {
  const state = { available: false, directory: '', queue: [], queuedBytes: 0, running: null, bytes: 0, dropped: 0, metadata: null };
  const timestamp = () => { const value = Number(now()); return Number.isFinite(value) && value >= 0 && value <= 253402300799999 ? value : Date.now(); };
  const logger = {
    get directory() { return state.directory; },
    get nativeDirectory() { return state.available ? state.directory : ''; },
    get available() { return state.available; },
    async record(event, fields = {}) {
      try {
        if (!state.available || !EVENTS.has(event)) return;
        const clean = sanitize(fields);
        const line = JSON.stringify({ at: new Date(timestamp()).toISOString(), event, ...clean }) + '\n';
        const bytes = Buffer.byteLength(line);
        if (state.queue.length >= QUEUE_COUNT || state.queuedBytes + bytes > QUEUE_BYTES) { state.dropped++; return; }
        state.queue.push({ line, bytes, pid: event.startsWith('launch_') ? clean.pid : undefined }); state.queuedBytes += bytes;
        pump();
      } catch {}
    },
    async flush() { try { while (state.running) await state.running; } catch {} },
  };
  const file = name => path.join(state.directory, name);
  async function write(line, bytes) {
    if (state.bytes + bytes > FILE_BYTES) {
      try { await io.unlink(file('events.2.jsonl')); } catch (error) { if (error.code !== 'ENOENT') throw error; }
      try { await io.rename(file('events.1.jsonl'), file('events.2.jsonl')); } catch (error) { if (error.code !== 'ENOENT') throw error; }
      await io.rename(file('events.jsonl'), file('events.1.jsonl'));
      state.bytes = 0;
    }
    await io.appendFile(file('events.jsonl'), line, { encoding: 'utf8', mode: 0o600 });
    state.bytes += bytes;
  }
  async function drain() {
    while (state.queue.length) {
      const entry = state.queue.shift(); state.queuedBytes -= entry.bytes;
      if (entry.pid && entry.pid !== state.metadata.appPid && !state.metadata.nativePids.includes(entry.pid)) {
        if (state.metadata.nativePids.length < 64) state.metadata.nativePids.push(entry.pid);
        else state.metadata.unknownNativeProcesses = true;
        await io.writeFile(file('session.pending'), JSON.stringify(state.metadata) + '\n', { mode: 0o600 });
        await io.rename(file('session.pending'), file('session.json'));
      }
      await write(entry.line, entry.bytes);
    }
    if (state.dropped) {
      const line = JSON.stringify({ at: new Date(timestamp()).toISOString(), event: 'events_dropped', count: state.dropped }) + '\n';
      state.dropped = 0;
      await write(line, Buffer.byteLength(line));
    }
  }
  function pump() {
    if (state.running || !state.available) return;
    state.running = drain().catch(() => {
      state.available = false; state.queue.length = 0; state.queuedBytes = 0;
    }).finally(() => { state.running = null; if (state.queue.length) pump(); });
  }
  try {
    if (typeof rootDir !== 'string' || !path.isAbsolute(rootDir)) return logger;
    await io.mkdir(rootDir, { recursive: true });
    const rootStat = await io.lstat(rootDir);
    if (!rootStat.isDirectory() || rootStat.isSymbolicLink()) return logger;
    const root = await io.realpath(rootDir), createdAt = timestamp();
    const name = new Date(createdAt).toISOString().replace(/[:.]/g, '-') + '_' + randomUUID();
    state.directory = path.join(root, name);
    await io.mkdir(state.directory, { mode: 0o700 });
    state.metadata = { format: FORMAT, directory: name, createdAt, appPid: process.pid, nativePids: [], ...sanitize({ appVersion }) };
    await io.writeFile(file('session.json'), JSON.stringify(state.metadata) + '\n', { flag: 'wx', mode: 0o600 });
    await io.writeFile(file('events.jsonl'), '', { flag: 'wx', mode: 0o600 });
    state.available = true;
    try { await prune(io, root, state.directory, createdAt); } catch {}
  } catch { state.available = false; }
  return logger;
}

module.exports = { createDiagnostics };
