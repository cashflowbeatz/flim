'use strict';
const sessions = new Map(), transfers = new Map();
const api = window.peerTransport;
const notify = m => api.event(m).catch(() => {});
function finish(t, failed = false) {
  if (transfers.get(t.id) !== t) return;
  transfers.delete(t.id); clearTimeout(t.timer); clearTimeout(t.idle);
  try {t.channel?.close();} catch {}
  for (const wait of t.waits || []) wait();
  if (failed && t.get) {
    if (!t.started && t.index < t.candidates.length) {attempt(t); return;}
    void notify({type: 'failed', session: t.session, id: t.id});
  }
}
function attach(t) {
  const ch = t.channel; ch.binaryType = 'arraybuffer'; t.waits = []; t.sent = t.acked = 0;
  t.queue = Promise.resolve(); transfers.set(t.id, t);
  ch.onclose = () => finish(t, true); ch.onerror = () => finish(t, true);
  ch.onmessage = event => {
    if (transfers.get(t.id) !== t) return;
    const queued = event.data instanceof ArrayBuffer ? event.data.byteLength : typeof event.data === 'string' ? event.data.length : 1048576;
    t.queued = (t.queued || 0) + queued;
    if (t.queued > 1048576) return finish(t, true);
    t.queue = t.queue.then(async () => {
      if (transfers.get(t.id) !== t) return;
      if (event.data instanceof ArrayBuffer) {
        if (!t.get || !t.started || event.data.byteLength > 32768) throw new Error('invalid_chunk');
        await api.event({type: 'chunk', session: t.session, id: t.id, data: event.data});
        t.acked += event.data.byteLength; ch.send(JSON.stringify({t: 'ack', bytes: t.acked})); return;
      }
      if (typeof event.data !== 'string' || event.data.length > 512) throw new Error('invalid_control');
      const m = JSON.parse(event.data);
      if (t.get) {
        if (m.t === 'start' && m.bytes === t.bytes && !t.started) {t.started = true; clearTimeout(t.timer); await api.event({type: 'start', session: t.session, id: t.id, bytes: m.bytes});}
        else if (m.t === 'end' && t.started) {await api.event({type: 'end', session: t.session, id: t.id}); finish(t);}
        else if (m.t === 'missing') finish(t, true);
        else throw new Error('invalid_response');
      } else if (m.t === 'want' && !t.requested && /^[a-f0-9]{64}$/.test(m.hash) && Number.isSafeInteger(m.bytes) && m.bytes > 0 && m.bytes <= 90000000) {
        t.requested = true; clearTimeout(t.idle);
        const ok = await api.event({type: 'serve', session: t.session, id: t.id, hash: m.hash, bytes: m.bytes});
        if (!ok) {ch.send(JSON.stringify({t: 'missing'})); t.timer = setTimeout(() => finish(t), 200);}
      } else if (m.t === 'ack' && Number.isSafeInteger(m.bytes) && m.bytes >= t.acked && m.bytes <= t.sent) {
        t.acked = m.bytes; for (const wait of t.waits.splice(0)) wait();
      } else throw new Error('invalid_request');
    }).catch(() => finish(t, true)).finally(() => {t.queued -= queued;});
  };
}
function attempt(t) {
  const s = sessions.get(t.session), peer = t.candidates[t.index++], p = s?.peers.get(peer);
  if (!p || p.pc.connectionState !== 'connected') {if (t.index < t.candidates.length) return attempt(t); return void notify({type: 'failed', session: t.session, id: t.id});}
  t.started = false; t.channel = p.pc.createDataChannel('flim-file-v1');
  attach(t); t.timer = setTimeout(() => finish(t, true), 1000);
  t.channel.onopen = () => t.channel.send(JSON.stringify({t: 'want', hash: t.hash, bytes: t.bytes}));
}
function reset(id) {
  const s = sessions.get(id); sessions.delete(id);
  for (const p of s?.peers.values() || []) p.pc.close();
  for (const t of [...transfers.values()]) if (t.session === id) finish(t);
}
function peer(s, id) {
  if (s.peers.has(id)) return s.peers.get(id);
  const pc = new RTCPeerConnection({iceServers: [{urls: 'stun:stun.cloudflare.com:3478'}], iceCandidatePoolSize: 0});
  const p = {pc, queue: Promise.resolve()}; s.peers.set(id, p);
  const signal = signal => notify({type: 'signal', session: s.id, to: id, signal});
  pc.onicecandidate = e => {if (e.candidate) void signal({type: 'ice', candidate: e.candidate.toJSON()});};
  pc.onconnectionstatechange = () => {void notify({type: 'ready', session: s.id, peer: id, ready: pc.connectionState === 'connected'});};
  pc.ondatachannel = e => {
    if (e.channel.label === 'flim-control-v1') {if (p.control) e.channel.close(); else p.control = e.channel; return;}
    if (e.channel.label !== 'flim-file-v1' || transfers.size >= 32) return e.channel.close();
    const t = {id: crypto.randomUUID(), session: s.id, channel: e.channel, get: false};
    attach(t); t.timer = setTimeout(() => finish(t), 120000); t.idle = setTimeout(() => finish(t), 5000);
  };
  if (s.you < id) {
    p.control = pc.createDataChannel('flim-control-v1');
    p.queue = p.queue.then(async () => {await pc.setLocalDescription(await pc.createOffer()); await signal({type: 'offer', sdp: pc.localDescription.sdp});}).catch(() => pc.close());
  }
  return p;
}
async function command(m) {
  if (m.type === 'reset') return reset(m.session);
  if (m.type === 'peers') {
    let s = sessions.get(m.session);
    if (!s) {s = {id: m.session, you: m.you, peers: new Map()}; sessions.set(m.session, s);}
    for (const [id, p] of s.peers) if (!m.peers.includes(id)) {p.pc.close(); s.peers.delete(id); void notify({type: 'ready', session: s.id, peer: id, ready: false});}
    for (const id of m.peers) if (id !== s.you) peer(s, id);
  } else if (m.type === 'signal') {
    const s = sessions.get(m.session), p = s?.peers.get(m.from); if (!p) return;
    p.queue = p.queue.then(async () => {
      if (m.signal.type === 'ice') await p.pc.addIceCandidate(m.signal.candidate);
      else {
        await p.pc.setRemoteDescription(m.signal);
        if (m.signal.type === 'offer') {await p.pc.setLocalDescription(await p.pc.createAnswer()); await notify({type: 'signal', session: s.id, to: m.from, signal: {type: 'answer', sdp: p.pc.localDescription.sdp}});}
      }
    }).catch(() => p.pc.close());
  } else if (m.type === 'get') {
    const s = sessions.get(m.session), candidates = [...(s?.peers || [])].filter(([,p]) => p.pc.connectionState === 'connected').map(([id]) => id);
    if (!candidates.length) return void notify({type: 'failed', session: m.session, id: m.id});
    attempt({...m, get: true, candidates, index: 0});
  } else {
    const t = transfers.get(m.id); if (!t || t.session !== m.session) throw new Error('transfer_closed');
    if (m.type === 'cancel') return finish(t);
    if (m.type === 'start') t.channel.send(JSON.stringify({t: 'start', bytes: m.bytes}));
    else if (m.type === 'end') {t.channel.send(JSON.stringify({t: 'end'})); t.timer = setTimeout(() => finish(t), 8000);}
    else if (m.type === 'chunk') {
      const data = new Uint8Array(m.data);
      while (transfers.get(t.id) === t && (t.sent - t.acked >= 524288 || t.channel.bufferedAmount > 524288)) {
        await new Promise((resolve, reject) => {const timer = setTimeout(() => reject(new Error('peer_backpressure')), 8000); t.waits.push(() => {clearTimeout(timer); resolve();});});
      }
      if (transfers.get(t.id) !== t || t.channel.readyState !== 'open') throw new Error('transfer_closed');
      t.channel.send(data); t.sent += data.byteLength;
    }
  }
}
api.onCommand(m => {void command(m).then(() => {if (m.reply) api.reply(m.reply, true);}).catch(() => {if (m.reply) api.reply(m.reply, false);});});
