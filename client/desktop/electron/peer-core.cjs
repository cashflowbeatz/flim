'use strict';

const http = require('node:http');
const fs = require('node:fs/promises');
const {randomBytes, randomUUID} = require('node:crypto');
const {WebSocket} = require('ws');
const path = require('node:path');

const HASH = /^[a-f0-9]{64}$/;
const LIMIT = 90_000_000;
const CHUNK = 32 * 1024;
const json = (res, status, body = {}) => {res.writeHead(status, {'content-type': 'application/json', 'cache-control': 'no-store'}); res.end(JSON.stringify(body));};
async function body(req) {
  const chunks = []; let size = 0;
  for await (const chunk of req) {size += chunk.length; if (size > 32768) throw new Error('body_limit'); chunks.push(chunk);}
  return JSON.parse(Buffer.concat(chunks).toString('utf8'));
}

class PeerFiles {
  constructor({relay, transport, record = () => {}, alive = pid => {try {process.kill(pid, 0); return true;} catch {return false;}}, idle = () => {}, connect = url => new WebSocket(url, {maxPayload: 65536, handshakeTimeout: 5000})}) {
    this.relay = relay; this.transport = transport; this.record = record; this.alive = alive; this.idle = idle; this.connect = connect;
    this.sessions = new Map(); this.transfers = new Map(); this.server = null; this.starting = null; this.closed = false;
  }
  get active() {return [...this.sessions.values()].some(s => s.pid && this.alive(s.pid));}
  async listen() {
    if (this.server) return;
    if (this.starting) return this.starting;
    this.starting = (async () => {
      const server = http.createServer((req, res) => {void this.request(req, res).catch(() => {if (!res.headersSent) json(res, 400); else res.destroy();});});
      server.requestTimeout = 15000; server.headersTimeout = 10000; server.maxConnections = 16;
      server.on('clientError', (_error, socket) => socket.destroy());
      await new Promise((resolve, reject) => {server.once('error', reject); server.listen(0, '127.0.0.1', resolve);});
      this.server = server;
      this.timer = setInterval(() => {
        for (const s of this.sessions.values()) if (s.pid ? !this.alive(s.pid) : Date.now() - s.created > 120000) this.drop(s);
      }, 3000); this.timer.unref();
    })().finally(() => {this.starting = null;});
    return this.starting;
  }
  async prepare(code) {
    if (this.closed || !/^\d{8}$/.test(code) || this.sessions.size >= 8) throw new Error('peer_capacity');
    await this.listen();
    if (this.closed || this.sessions.size >= 8) throw new Error('peer_capacity');
    const token = randomBytes(32).toString('hex');
    const s = {id: randomUUID(), token, code, created: Date.now(), generation: 0, offers: new Map(), ready: new Set(), pid: 0, incoming: 0, outgoing: 0};
    this.sessions.set(token, s);
    return {env: {FLIM_PEER_URL: `http://127.0.0.1:${this.server.address().port}`, FLIM_PEER_TOKEN: token},
      launched: pid => {s.pid = pid;}, cancel: () => this.drop(s)};
  }
  drop(s) {
    if (!this.sessions.delete(s.token)) return;
    clearTimeout(s.reconnect); s.ws?.close(); this.transport.send({type: 'reset', session: s.id});
    for (const [id, t] of this.transfers) if (t.session === s) this.finish(id, new Error('session_ended'));
    s.offers.clear(); s.ready.clear(); this.idle();
  }
  async request(req, res) {
    if (req.headers.origin || !/^127\.0\.0\.1:\d+$/.test(req.headers.host || '')) return json(res, 403);
    const s = this.sessions.get(req.headers['x-flim-key']);
    if (!s) return json(res, 403);
    const u = new URL(req.url, 'http://127.0.0.1');
    if (req.method === 'POST' && u.pathname === '/connect') {
      const data = await body(req);
      if (!HASH.test(data.token) || !Number.isSafeInteger(data.generation) || data.generation < 1 || data.generation <= s.generation) return json(res, 409);
      s.generation = data.generation; s.credential = data.token;
      clearTimeout(s.reconnect); s.ws?.close(); s.ws = null; s.offers.clear(); s.ready.clear();
      for (const [id, t] of this.transfers) if (t.session === s) this.finish(id, new Error('session_changed'));
      this.transport.send({type: 'reset', session: s.id}); this.attach(s); return json(res, 200);
    }
    if (req.method === 'POST' && u.pathname === '/offer') {
      const data = await body(req);
      if (data.generation !== s.generation || !HASH.test(data.h) || !Number.isSafeInteger(data.s) || data.s < 1 || data.s > LIMIT || typeof data.p !== 'string' || data.p.length > 22000) return json(res, 400);
      const decoded = Buffer.from(data.p, 'base64');
      if (decoded.length % 2 || decoded.toString('base64') !== data.p) return json(res, 400);
      const file = decoded.toString('utf16le');
      if (!path.isAbsolute(file) || file.includes('\0') || !file.isWellFormed()) return json(res, 400);
      const stat = await fs.stat(file);
      if (!this.sessions.has(s.token) || data.generation !== s.generation) return json(res, 409);
      if (!stat.isFile() || stat.size !== data.s || s.offers.size >= 2000 && !s.offers.has(data.h)) return json(res, 409);
      s.offers.set(data.h, {file, bytes: stat.size, mtime: stat.mtimeMs}); return json(res, 200);
    }
    const hash = u.pathname.slice(6), bytes = Number(u.searchParams.get('bytes'));
    if (req.method !== 'GET' || !u.pathname.startsWith('/blob/') || !HASH.test(hash) || !Number.isSafeInteger(bytes) || bytes < 1 || bytes > LIMIT || Number(u.searchParams.get('generation')) !== s.generation) return json(res, 400);
    if (!s.ready.size || s.incoming >= 2) return json(res, 404);
    s.incoming++;
    const id = randomUUID();
    try {
      await new Promise((resolve, reject) => {
        const t = {session: s, res, bytes, done: 0, resolve, reject};
        t.timer = setTimeout(() => this.finish(id, new Error('peer_timeout')), 120000);
        t.first = setTimeout(() => this.finish(id, new Error('peer_unavailable')), 1500);
        this.transfers.set(id, t);
        res.once('close', () => {if (this.transfers.has(id)) this.finish(id, new Error('receiver_closed'));});
        this.transport.send({type: 'get', session: s.id, id, hash, bytes});
      });
      this.record('peer_download', {bytes});
    } catch {if (!res.headersSent) json(res, 404); else res.destroy();}
    finally {s.incoming--;}
  }
  attach(s) {
    if (!this.sessions.has(s.token)) return;
    const endpoint = new URL(`/peer/${s.code}`, this.relay); endpoint.protocol = endpoint.protocol === 'https:' ? 'wss:' : 'ws:';
    let ws;
    try {ws = this.connect(endpoint.href);} catch {s.reconnect = setTimeout(() => this.attach(s), 5000); return;}
    s.ws = ws;
    ws.on('open', () => {if (s.ws === ws) ws.send(JSON.stringify({t: 'files.hello', token: s.credential}));});
    ws.on('message', raw => {
      if (s.ws !== ws || !this.sessions.has(s.token)) return;
      try {
        const m = JSON.parse(String(raw));
        if (m.t === 'files.peers' && typeof m.you === 'string' && Array.isArray(m.peers) && m.peers.length <= 8 && m.peers.every(p => typeof p === 'string' && /^[\w-]{1,64}$/.test(p))) this.transport.send({type: 'peers', session: s.id, you: m.you, peers: m.peers});
        else if (m.t === 'files.signal') this.transport.send({type: 'signal', session: s.id, from: m.from, signal: m.signal});
      } catch {}
    });
    ws.on('error', () => {});
    ws.on('close', (code) => {
      if (s.ws !== ws || !this.sessions.has(s.token)) return;
      s.ws = null; s.ready.clear(); this.transport.send({type: 'reset', session: s.id});
      for (const [id, t] of this.transfers) if (t.session === s) this.finish(id, new Error('signaling_closed'));
      if (code !== 4000 && code !== 4001) s.reconnect = setTimeout(() => this.attach(s), 5000);
    });
  }
  finish(id, error) {
    const t = this.transfers.get(id); if (!t) return;
    this.transfers.delete(id); clearTimeout(t.timer); clearTimeout(t.first);
    this.transport.send({type: 'cancel', session: t.session.id, id});
    if (error) t.reject(error); else {t.res.end(); t.resolve();}
  }
  async event(m) {
    const s = [...this.sessions.values()].find(s => s.id === m?.session); if (!s) return;
    if (m.type === 'signal') {if (s.ws?.readyState === 1) s.ws.send(JSON.stringify({t: 'files.signal', to: m.to, signal: m.signal}));}
    else if (m.type === 'ready') {if (m.ready) s.ready.add(m.peer); else s.ready.delete(m.peer);}
    else if (m.type === 'serve') {
      if (s.outgoing >= 2 || !HASH.test(m.hash)) return false;
      const offer = s.offers.get(m.hash); if (!offer || offer.bytes !== m.bytes) return false;
      s.outgoing++;
      const id = m.id, generation = s.generation;
      let file;
      try {
        file = await fs.open(offer.file, 'r');
        const stat = await file.stat();
        if (!this.sessions.has(s.token) || generation !== s.generation || !stat.isFile() || stat.size !== offer.bytes || stat.mtimeMs !== offer.mtime) throw new Error('source_changed');
      } catch {s.outgoing--; await file?.close().catch(() => {}); return false;}
      void (async () => {
        try {
          this.transport.send({type: 'start', session: s.id, id, bytes: offer.bytes});
          let offset = 0;
          while (offset < offer.bytes && this.sessions.has(s.token) && generation === s.generation) {
            const chunk = Buffer.allocUnsafe(Math.min(CHUNK, offer.bytes - offset));
            const {bytesRead} = await file.read(chunk, 0, chunk.length, offset); if (!bytesRead) throw new Error('source_changed');
            if (!this.sessions.has(s.token) || generation !== s.generation) throw new Error('session_ended');
            await this.transport.chunk({type: 'chunk', session: s.id, id, data: chunk.subarray(0, bytesRead)}); offset += bytesRead;
          }
          const after = await file.stat();
          if (offset !== offer.bytes || generation !== s.generation || after.size !== offer.bytes || after.mtimeMs !== offer.mtime) throw new Error('source_changed');
          this.transport.send({type: 'end', session: s.id, id});
        } catch {this.transport.send({type: 'cancel', session: s.id, id});}
        finally {s.outgoing--; await file.close().catch(() => {});}
      })();
      return true;
    } else {
      const t = this.transfers.get(m.id); if (!t || t.session !== s) return;
      if (m.type === 'start' && m.bytes === t.bytes) {clearTimeout(t.first); t.started = true; t.res.writeHead(200, {'content-type': 'application/octet-stream', 'content-length': t.bytes, 'cache-control': 'no-store'});}
      else if (m.type === 'chunk' && t.started) {
        const data = Buffer.from(m.data); if (!data.length || data.length > CHUNK || t.done + data.length > t.bytes) return this.finish(m.id, new Error('invalid_length'));
        t.done += data.length;
        if (!t.res.write(data)) await new Promise(resolve => {
          const done = () => {t.res.off('drain', done); t.res.off('close', done); resolve();};
          t.res.once('drain', done); t.res.once('close', done);
        });
      } else if (m.type === 'end' && t.started && t.done === t.bytes) this.finish(m.id);
      else if (m.type === 'failed') this.finish(m.id, new Error('peer_failed'));
      else this.finish(m.id, new Error('invalid_transfer'));
    }
  }
  async close() {this.closed = true; await this.starting; clearInterval(this.timer); for (const s of [...this.sessions.values()]) this.drop(s); if (this.server) {this.server.closeAllConnections(); await new Promise(resolve => this.server.close(resolve)); this.server = null;}}
}
module.exports = {PeerFiles, CHUNK};
