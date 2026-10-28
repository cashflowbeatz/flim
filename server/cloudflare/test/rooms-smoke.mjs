import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";

const base = new URL(process.env.FLIM_TEST_RELAY ?? "https://flim-relay.cashflowbeatz.workers.dev");
assert.ok(base.protocol === "https:" || (base.protocol === "http:" && ["127.0.0.1", "localhost"].includes(base.hostname)), "Use HTTPS or loopback HTTP");
const endpoint = path => new URL(path, base);
const sockets = [];
const compat = { protocol: 1, fl: "26.1.7.5654", wire: "flim-25-26-v1" };
const checks = [];
const check = (name, condition) => { assert.ok(condition, name); checks.push(name); };
async function api(path, body) {
  const response = await fetch(endpoint(path), { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(body), signal: AbortSignal.timeout(15_000) });
  return { status: response.status, body: await response.json() };
}
async function connect(room, role, id, owner = room.hostToken) {
  const url = endpoint(room.websocket); url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(url); sockets.push(ws); const messages = [], wake = []; let closed = false;
  const next = async type => {
    const until = Date.now() + 10_000;
    for (;;) {
      const error = messages.find(m => m.t === "error"); if (error) throw new Error("Room socket error: " + error.code);
      const index = messages.findIndex(m => m.t === type); if (index >= 0) return messages.splice(index, 1)[0];
      if (closed || Date.now() >= until) throw new Error("Room socket response unavailable: " + type);
      await new Promise(resolve => { const timer = setTimeout(resolve, 100); wake.push(() => { clearTimeout(timer); resolve(); }); });
    }
  };
  ws.addEventListener("message", event => { messages.push(JSON.parse(event.data)); for (const done of wake.splice(0)) done(); });
  ws.addEventListener("close", () => { closed = true; for (const done of wake.splice(0)) done(); });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("Room socket opening timed out")), 10_000);
    ws.addEventListener("open", () => { clearTimeout(timer); resolve(); }, { once: true });
    ws.addEventListener("error", () => { clearTimeout(timer); reject(new Error("Room socket could not open")); }, { once: true });
  });
  ws.send(JSON.stringify({ t: role, transport: 2, compat, owner, password: room.password, session: room.room, user: { id, name: "Room validation" } }));
  const welcome = await next("welcome"); await next("lock_state"); messages.length = 0;
  return { ws, welcome, messages, next, send: value => ws.send(JSON.stringify(value)) };
}
try {
  const health = await fetch(endpoint("/health"), { signal: AbortSignal.timeout(15_000) });
  check("relay health", health.status === 200 && (await health.text()).trim() === "flim-server ok");
  if (process.env.FLIM_ROOM_SMOKE_CREATE !== "yes") {
    const probe = await fetch(endpoint("/rooms"), { signal: AbortSignal.timeout(15_000) });
    console.log(JSON.stringify({ mode: "read-only", health: "passed", roomApi: probe.status === 405 ? "available" : "not available", roomApiStatus: probe.status }));
  } else {
    const first = await api("/rooms", {}), second = await api("/rooms", {});
    check("two scoped room reservations", first.status === 201 && second.status === 201);
    const a = first.body, b = second.body;
    check("independent numeric invitations", /^\d{8}$/.test(a.room) && /^\d{8}$/.test(b.room) && a.room !== b.room);
    check("status requires room password", (await api("/rooms/status", { room: a.room, password: b.password })).status === 404);
    check("reserved room waits for host", (await api("/rooms/join", { room: a.room })).body.code === "room_not_ready");
    const tag = randomUUID();
    const ah = await connect(a, "host", "host-a-" + tag), bh = await connect(b, "host", "host-b-" + tag);
    check("room path reaches its own welcome", ah.welcome.session === a.room && bh.welcome.session === b.room);
    const ja = await api("/rooms/join", { room: a.room }), jb = await api("/rooms/join", { room: b.room });
    check("join issues member credentials only", ja.status === 200 && jb.status === 200 && !ja.body.hostToken && !jb.body.hostToken);
    const am = await connect(ja.body, "join", "member-a-" + tag), bm = await connect(jb.body, "join", "member-b-" + tag);
    am.send({ t: "op", op: { kind: "cur", marker: "a" } });
    bm.send({ t: "op", op: { kind: "cur", marker: "b" } });
    check("member edits reach their host", (await ah.next("op")).op.marker === "a" && (await bh.next("op")).op.marker === "b");
    const sa = await api("/rooms/status", { room: a.room, password: a.password });
    const sb = await api("/rooms/status", { room: b.room, password: b.password });
    check("participant status is isolated", sa.body.state === "active" && sb.body.state === "active" && sa.body.peers.length === 2 && sb.body.peers.length === 2 && sa.body.peers.every(p => p.id.includes("-a-")) && sb.body.peers.every(p => p.id.includes("-b-")));
    check("no cross-room edit delivery", !ah.messages.some(m => m.t === "op") && !bh.messages.some(m => m.t === "op"));
    ah.ws.close(1000, "room validation complete");
    let ended;
    for (let i = 0; i < 10; i++) {
      ended = await api("/rooms/status", { room: a.room, password: a.password }); if (ended.status === 404) break;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    check("host departure ends only its room", ended.status === 404 && (await api("/rooms/status", { room: b.room, password: b.password })).body.state === "active");
    console.log(JSON.stringify({ mode: "scoped-room-smoke", passed: checks.length, checks, roomReservationsUsed: 2, r2Writes: 0 }));
  }
} catch (error) {
  console.error("Room API smoke failed:", error instanceof Error ? error.message : "unknown error"); process.exitCode = 1;
} finally {
  await Promise.all(sockets.map(ws => new Promise(resolve => {
    if (ws.readyState === WebSocket.CLOSED) return resolve();
    const timeout = setTimeout(resolve, 2000); ws.addEventListener("close", () => { clearTimeout(timeout); resolve(); }, { once: true });
    try { ws.close(1000, "room validation complete"); } catch { clearTimeout(timeout); resolve(); }
  })));
}
