const assert = require("node:assert/strict");
const test = require("node:test");
const { WebSocket } = require("ws");
const { Session } = require("../dist/session");
const { LOCK_TTL_MS } = require("../dist/protocol");

function client(id) {
  return {
    user: { id, name: id }, role: id === "host" ? "leader" : "member", alive: true, ip: "127.0.0.1",
    ws: { readyState: WebSocket.OPEN, bufferedAmount: 0, sent: [],
      send(data, done) { this.sent.push(JSON.parse(data)); done?.(); }, close() {}, terminate() {} },
  };
}

function fixture(t) {
  const originalNow = Date.now;
  let now = 100_000;
  Date.now = () => now;
  t.after(() => { Date.now = originalNow; });
  const host = client("host"), member = client("member");
  const session = new Session("01", "pw", host, null, () => {});
  session.add(member);
  return { host, member, session, advance(ms) { now += ms; } };
}

test("expired locks stop blocking ops at the TTL, without waiting for the sweep", (t) => {
  const { host, member, session, advance } = fixture(t);
  session.acquire(host, "param:1");
  advance(LOCK_TTL_MS);
  host.ws.sent.length = member.ws.sent.length = 0;
  session.relayOp(member, { kind: "param.set", key: "param:1", v: 7 });
  assert.equal(member.ws.sent.some((m) => m.code === "locked"), false);
  assert.equal(host.ws.sent.at(-1).op?.v, 7);
  assert.equal(host.ws.sent[0].t, "lock_released");
});

test("editing cannot revive an expired lock", (t) => {
  const { host, member, session, advance } = fixture(t);
  session.acquire(host, "param:1");
  advance(LOCK_TTL_MS);
  session.touchLocksOf(host.user.id);
  session.relayOp(member, { kind: "param.set", key: "param:1", v: 8 });
  assert.equal(member.ws.sent.some((m) => m.code === "locked"), false);
  assert.equal(host.ws.sent.at(-1).op?.v, 8);
});

test("expired locks do not consume the lock quota", (t) => {
  const { host, member, session, advance } = fixture(t);
  for (let i = 0; i < 4096; i++) session.acquire(host, `param:${i}`);
  advance(LOCK_TTL_MS);
  session.acquire(member, "param:new");
  assert.equal(member.ws.sent.at(-1).granted, true);
});

test("late joiners only receive locks that are still active", (t) => {
  const { host, session, advance } = fixture(t);
  session.acquire(host, "param:expired");
  advance(LOCK_TTL_MS - 1);
  session.acquire(host, "param:active");
  advance(1);
  const newcomer = client("newcomer");
  session.add(newcomer);
  assert.deepEqual(newcomer.ws.sent.find((m) => m.t === "lock_state").locks,
    [{ key: "param:active", owner: "host" }]);
});

test("accepted edits renew active locks, cursor traffic and rejected edits do not", (t) => {
  const { host, member, session, advance } = fixture(t);
  session.acquire(host, "param:host");
  session.acquire(member, "param:member");
  advance(LOCK_TTL_MS - 1);
  session.relayOp(host, { kind: "param.set", key: "param:host", v: 9 });
  session.relayOp(member, { kind: "param.set", key: "param:host", v: 10 });
  session.relayOp(member, { kind: "cur", x: 1, y: 2 });
  advance(1);
  const newcomer = client("newcomer");
  session.add(newcomer);
  assert.deepEqual(newcomer.ws.sent.find((m) => m.t === "lock_state").locks,
    [{ key: "param:host", owner: "host" }]);
  assert.equal(member.ws.sent.some((m) => m.code === "locked"), true);
});

test("only the host can broadcast project and full-state replacements", (t) => {
  const { host, member, session } = fixture(t);
  for (const kind of ["project.load", "plug.full", "notes.full", "state.full"]) {
    host.ws.sent.length = member.ws.sent.length = 0;
    session.relayOp(member, { kind, data: "test" });
    assert.equal(member.ws.sent.at(-1)?.code, "not_leader", kind);
    assert.equal(host.ws.sent.length, 0, kind);
    session.relayOp(host, { kind, data: "test" });
    assert.equal(member.ws.sent.at(-1).op?.kind, kind);
  }
  host.ws.sent.length = 0;
  session.relayOp(member, { kind: "state.req" });
  session.relayOp(member, { kind: "plug.state" });
  assert.deepEqual(host.ws.sent.map((m) => m.op.kind), ["state.req", "plug.state"]);
});

test("loading a new project releases old locks before relaying the project", (t) => {
  const { host, member, session } = fixture(t);
  session.acquire(host, "param:1");
  session.acquire(member, "plug:ch:0");
  host.ws.sent.length = member.ws.sent.length = 0;
  session.relayOp(host, { kind: "project.load", data: "new-project" });
  assert.deepEqual(member.ws.sent.map((m) => m.t), ["lock_released", "lock_released", "op"]);
  assert.deepEqual(host.ws.sent.map((m) => m.t), ["lock_released", "lock_released"]);
  const newcomer = client("newcomer");
  session.add(newcomer);
  assert.deepEqual(newcomer.ws.sent.find((m) => m.t === "lock_state").locks, []);
  session.acquire(member, "param:1");
  assert.equal(member.ws.sent.at(-1).granted, true, "new project can immediately reuse an old unit ID");
  session.acquire(host, "plug:ch:0");
  assert.equal(host.ws.sent.at(-1).granted, true);
});

test("a member's rejected project load cannot clear anyone's locks", (t) => {
  const { host, member, session } = fixture(t);
  session.acquire(host, "param:1");
  host.ws.sent.length = member.ws.sent.length = 0;
  session.relayOp(member, { kind: "project.load", data: "rejected" });
  assert.equal(member.ws.sent.at(-1).code, "not_leader");
  assert.equal(host.ws.sent.length, 0);
  session.acquire(member, "param:1");
  assert.equal(member.ws.sent.at(-1).granted, false);
});

test("full-state updates preserve locks while project_swap releases them", (t) => {
  const { host, member, session } = fixture(t);
  session.acquire(member, "param:1");
  for (const kind of ["plug.full", "notes.full", "state.full"]) {
    session.relayOp(host, { kind, data: "state" });
    session.acquire(host, "param:1");
    assert.equal(host.ws.sent.at(-1).granted, false, kind);
  }
  host.ws.sent.length = member.ws.sent.length = 0;
  session.projectSwap(host, "new-project");
  assert.deepEqual(member.ws.sent.map((m) => m.t), ["lock_released", "project_swap"]);
  assert.equal(member.ws.sent.at(-1).epoch, 1);
  assert.equal(host.ws.sent[0].t, "lock_released");
  session.acquire(host, "param:1");
  assert.equal(host.ws.sent.at(-1).granted, true);
});
