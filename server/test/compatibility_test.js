const assert = require("node:assert/strict");
const test = require("node:test");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { once } = require("node:events");
const { spawn } = require("node:child_process");
const WebSocket = require("ws");
const { Session } = require("../dist/session");
const { LOCK_TTL_MS, validOp } = require("../dist/protocol");
const { flVersion, validCompatibility, compareFlVersions, SHARED_WIRE } = require("../dist/compatibility");

const compat = (fl) => ({ protocol: 1, fl, wire: SHARED_WIRE });
function client(id, fl) {
  return {
    user: { id, name: id }, role: id === "host" ? "leader" : "member", alive: true, ip: "127.0.0.1",
    ...(fl === undefined ? {} : { compat: compat(fl) }),
    ws: { readyState: WebSocket.OPEN, bufferedAmount: 0, sent: [],
      send(data, done) { this.sent.push(JSON.parse(data)); done?.(); }, close() {}, terminate() {} },
  };
}
function fixture(hostFl = "25.2.5.5319", memberFl = "26.1.7.5654") {
  const host = client("host", hostFl), member = client("member", memberFl);
  const session = new Session("01", "pw", host, null, () => {});
  assert.equal(session.add(member), true);
  return { host, member, session };
}

test("only canonical complete FL 25/26 and exact reviewed FL24 descriptors select the shared wire", () => {
  for (const fl of ["24.2.2.4597", "25.0.0.0", "25.2.5.5319", "26.1.7.5654", "26.65535.65535.65535"]) {
    assert.ok(flVersion(fl), fl);
    assert.equal(validCompatibility(compat(fl)), true, fl);
  }
  for (const fl of ["24.1.2.3", "24.2.2.4596", "24.2.2.4598", "24.02.2.4597", "27.0.0.0", "025.2.5.5319", "25.02.5.5319", "25.2.5.05319",
    "25.2.5", "25.2.5.5319.1", "25.2.5.5319\n", "25.2.5.5319 ", "25.2.5.65536", "26.1.0.-1",
    "26.1e0.0.1", "26.1.0.+1", "26.1.0.１", "26.1.0.1x", null, 26]) {
    assert.equal(validCompatibility(compat(fl)), false, JSON.stringify(fl));
  }
  for (const value of [null, [], {}, { ...compat("25.1.0.1"), protocol: "1" },
    { ...compat("25.1.0.1"), protocol: 2 }, { ...compat("25.1.0.1"), wire: "unreviewed-wire" },
    { ...compat("25.1.0.1"), extra: true }]) assert.equal(validCompatibility(value), false);
  assert.ok(compareFlVersions("25.10.0.1", "25.2.9.65535") > 0);
  assert.ok(compareFlVersions("26.0.0.0", "25.65535.65535.65535") > 0);
  assert.throws(() => compareFlVersions("25.2.5", "26.1.7.5654"));
});

test("session admission keeps the host on the oldest build and never sends a rejected client its snapshot", () => {
  for (const [hostFl, memberFl, accepted] of [
    ["24.2.2.4597", "24.2.2.4597", true], ["24.2.2.4597", "25.2.5.5319", true],
    ["24.2.2.4597", "26.1.7.5654", true], ["25.2.5.5319", "24.2.2.4597", false],
    ["26.1.7.5654", "24.2.2.4597", false],
    ["25.2.5.5319", "25.2.5.5319", true], ["25.2.5.5319", "26.1.7.5654", true],
    ["25.2.5.5319", "25.10.0.1", true], ["26.1.7.5654", "25.2.5.5319", false],
    ["26.1.7.5654", "26.1.6.5639", false], ["25.2.5.5319", "25.2.3.5171", false],
    [undefined, "25.2.5.5319", false], ["25.2.5.5319", undefined, false], [undefined, undefined, true],
  ]) {
    const host = client("host", hostFl), member = client("member", memberFl);
    const session = new Session("01", "pw", host, null, () => {});
    session.setSnapshot(host, "private-project");
    host.ws.sent.length = 0;
    assert.equal(session.add(member), accepted, `${hostFl} <- ${memberFl}`);
    assert.equal(session.size, accepted ? 2 : 1);
    if (!accepted) {
      assert.deepEqual(member.ws.sent.map((m) => m.t), ["error"]);
      assert.equal(member.ws.sent[0].code, "compatibility");
      assert.equal(host.ws.sent.length, 0);
    }
  }
});

test("direct session admission also rejects malformed and unreviewed descriptors", () => {
  const host = client("host", "25.2.5.5319");
  const session = new Session("01", "pw", host, null, () => {});
  for (const descriptor of [null, {}, { ...compat("26.1.7.5654"), wire: "unknown" }, compat("26.1.7")]) {
    const member = client("member"); member.compat = descriptor;
    assert.equal(session.add(member), false);
    assert.deepEqual(member.ws.sent.map((m) => m.t), ["error"]);
  }
  const invalidHost = client("host"); invalidHost.compat = null;
  assert.throws(() => new Session("01", "pw", invalidHost, null, () => {}), /host compatibility/);
});

test("negotiated welcome and presence announce build transitions before the next edit", () => {
  const { host, member, session } = fixture();
  const initial = host.ws.sent.find((m) => m.t === "welcome");
  assert.deepEqual(initial.compatibility, { protocol: 1, host_fl: "25.2.5.5319", wire: SHARED_WIRE, mixed: false });
  const joined = member.ws.sent.find((m) => m.t === "welcome");
  assert.equal(joined.compatibility.mixed, true);
  assert.deepEqual(joined.peers.map((p) => p.fl), ["25.2.5.5319", "26.1.7.5654"]);
  const presenceIndex = host.ws.sent.findIndex((m) => m.t === "presence" && m.compatibility?.mixed);
  session.relayOp(member, { kind: "param.set", v: 3 });
  assert.ok(presenceIndex >= 0 && presenceIndex < host.ws.sent.findIndex((m) => m.t === "op"));
  assert.equal(host.ws.sent.at(-1).source_fl, "26.1.7.5654");
  session.remove(member.ws);
  assert.equal(host.ws.sent.at(-1).t, "presence");
  assert.equal(host.ws.sent.at(-1).compatibility.mixed, false);
});

test("legacy sessions keep the existing messages and extension operations", () => {
  const host = client("host"), member = client("member");
  const session = new Session("01", "pw", host, null, () => {});
  session.add(member);
  session.relayOp(member, { kind: "future.extension" });
  assert.equal(host.ws.sent.at(-1).op.kind, "future.extension");
  assert.equal("source_fl" in host.ws.sent.at(-1), false);
  assert.equal(host.ws.sent.some((m) => "compatibility" in m), false);
  assert.equal(member.ws.sent.find((m) => m.t === "welcome").peers.some((p) => "fl" in p), false);
});

test("mixed sessions relay known edits both ways but newer builds cannot send opaque presets or channels", () => {
  const { host, member, session } = fixture();
  const equalMember = client("same-build", "25.2.5.5319");
  assert.equal(session.add(equalMember), true);
  for (const kind of ["notes.set", "clips.set", "meta.set", "pat.op", "focus", "cur", "fx.clear", "chan.order", "param.set", "chan.del", "state.req"]) {
    session.relayOp(member, { kind });
    assert.equal(host.ws.sent.at(-1).op?.kind, kind);
    assert.equal(host.ws.sent.at(-1).source_fl, "26.1.7.5654");
    session.relayOp(host, { kind });
    assert.equal(member.ws.sent.at(-1).op?.kind, kind);
    assert.equal(member.ws.sent.at(-1).source_fl, "25.2.5.5319");
  }
  for (const kind of ["plug.state", "chan.add"]) {
    host.ws.sent.length = member.ws.sent.length = 0;
    session.relayOp(member, { kind, data: "newer-private-format" });
    assert.equal(member.ws.sent.at(-1).code, "compatibility");
    assert.equal(host.ws.sent.length, 0);
    session.relayOp(host, { kind, data: "older-format" });
    assert.equal(member.ws.sent.at(-1).op.data, "older-format");
    session.relayOp(equalMember, { kind, data: "same-as-host" });
    assert.equal(host.ws.sent.at(-1).op.data, "same-as-host");
  }
});

test("same-build negotiated sessions can send presets in both directions", () => {
  const { host, member, session } = fixture("26.1.7.5654", "26.1.7.5654");
  for (const kind of ["plug.state", "chan.add", "future.extension"]) {
    session.relayOp(member, { kind, data: "same-build" });
    assert.equal(host.ws.sent.at(-1).op.kind, kind);
    assert.equal(host.ws.sent.at(-1).source_fl, "26.1.7.5654");
  }
});

test("unknown mixed operations and newer opaque edits neither renew locks nor consume sequence numbers", (t) => {
  const originalNow = Date.now; let now = 100_000;
  Date.now = () => now; t.after(() => { Date.now = originalNow; });
  const { host, member, session } = fixture();
  session.acquire(member, "param:1");
  now += LOCK_TTL_MS - 1;
  host.ws.sent.length = member.ws.sent.length = 0;
  for (const kind of ["future.extension", "plug.state", "chan.add"]) {
    session.relayOp(member, { kind, key: "param:1" });
    assert.equal(member.ws.sent.at(-1).code, "compatibility");
    assert.equal(host.ws.sent.length, 0);
  }
  now++;
  session.acquire(host, "param:1");
  assert.equal(host.ws.sent.at(-1).granted, true, "rejected edits did not renew the old lock");
  session.relayOp(member, { kind: "cur" });
  assert.equal(host.ws.sent.at(-1).seq, 1);
  session.relayOp(host, { kind: "future.extension" });
  assert.equal(host.ws.sent.at(-1).code, "compatibility");
});

test("mixed full-state replacements remain host-only and no unregistered client can relay", () => {
  const { host, member, session } = fixture();
  for (const kind of ["project.load", "plug.full", "notes.full", "state.full"]) {
    host.ws.sent.length = member.ws.sent.length = 0;
    session.relayOp(member, { kind, data: "unauthorized" });
    assert.equal(member.ws.sent.at(-1).code, "not_leader");
    assert.equal(host.ws.sent.length, 0);
    session.relayOp(host, { kind, data: "host-state" });
    assert.equal(member.ws.sent.at(-1).source_fl, "25.2.5.5319");
  }
  const impostor = client("host", "25.2.5.5319");
  session.relayOp(impostor, { kind: "plug.full" });
  assert.equal(impostor.ws.sent.at(-1).code, "not_joined");
});

test("changing the caller's descriptor cannot change its admitted build or mixed policy", () => {
  const { host, member, session } = fixture();
  host.compat.fl = "26.1.7.5654";
  member.compat.fl = "25.2.5.5319";
  session.relayOp(member, { kind: "param.set" });
  assert.equal(host.ws.sent.at(-1).source_fl, "26.1.7.5654");
  session.relayOp(member, { kind: "plug.state" });
  assert.equal(member.ws.sent.at(-1).code, "compatibility");
  assert.equal(session.add(member), false);
  session.relayOp(member, { kind: "plug.state" });
  assert.equal(member.ws.sent.at(-1).code, "compatibility");
});

test("operations cannot spoof compatibility control fields through payload keys", () => {
  for (const key of ["source_fl", "host_fl", "compat", "compatibility"]) {
    assert.equal(validOp({ kind: "param.set", [key]: "25.2.5.5319" }), false);
    assert.equal(validOp({ kind: "param.set", data: [{ [key]: "25.2.5.5319" }] }), false);
    assert.equal(validOp({ kind: "param.set", data: { [key]: compat("25.2.5.5319") } }), false);
  }
});

const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(predicate, timeout = 5000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const result = predicate();
    if (result) return result;
    await delay(10);
  }
  throw new Error("Timed out waiting for compatibility test state");
}

test("the real relay negotiates mixed builds and refuses unsafe clients before project disclosure", async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "flim-compatibility-"));
  const child = spawn(process.execPath, [path.join(__dirname, "../dist/server.js")], {
    env: { ...process.env, PORT: "0", FLIM_BIND: "127.0.0.1", FLIM_PUBLIC: "1", FLIM_PROXY: "none",
      FLIM_OWNER_KEY: "compatibility-test-owner", FLIM_PASSWORD: "", FLIM_BLOB_DIR: root,
      FLIM_IDLE_MS: "0", FLIM_SESSION_MS: "0", FLIM_MAX_CONNS_PER_IP: "100" },
    stdio: ["ignore", "pipe", "pipe"], windowsHide: true,
  });
  let output = "";
  child.stdout.on("data", (part) => output += part);
  child.stderr.on("data", (part) => output += part);
  const sockets = [];
  t.after(async () => {
    sockets.forEach((ws) => ws.terminate());
    if (child.exitCode === null && child.signalCode === null) {
      const exited = once(child, "exit"); child.kill(); await exited;
    }
    assert.equal(path.dirname(path.resolve(root)), path.resolve(os.tmpdir()));
    assert.ok(path.basename(root).startsWith("flim-compatibility-"));
    fs.rmSync(root, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
  });
  const port = Number((await until(() => /listening on :(\d+)/.exec(output)))[1]);
  async function connect(id, hello) {
    const ws = new WebSocket(`ws://127.0.0.1:${port}`);
    sockets.push(ws); ws.msgs = [];
    ws.on("message", (data) => ws.msgs.push(JSON.parse(data.toString())));
    ws.json = (data) => ws.send(JSON.stringify(data));
    ws.wait = (predicate) => until(() => ws.msgs.find(predicate));
    await once(ws, "open");
    ws.json({ password: "test", user: { id, name: id }, ...hello });
    await ws.wait((m) => m.t === "welcome" || m.t === "error");
    return ws;
  }
  async function refused(id, hello) {
    const ws = await connect(id, hello);
    assert.equal(ws.msgs[0].code, "compatibility", id);
    if (ws.readyState !== WebSocket.CLOSED) await once(ws, "close");
    assert.deepEqual(ws.msgs.map((m) => m.t), ["error"], id);
  }
  for (const descriptor of [null, {}, compat("25.02.5.5319"), { ...compat("25.2.5.5319"), wire: "unknown" }]) {
    await refused(`bad-host-${JSON.stringify(descriptor)}`, { t: "host", compat: descriptor });
  }
  const host = await connect("host", { t: "host", compat: compat("25.2.5.5319") });
  const hostWelcome = host.msgs.find((m) => m.t === "welcome");
  assert.equal(hostWelcome.session, "01", "bad descriptors did not allocate slots");
  host.json({ t: "snapshot", flp: "private-host-project" });
  const member = await connect("member", { t: "join", session: "01", compat: compat("26.1.7.5654") });
  await member.wait((m) => m.t === "snapshot" && m.flp === "private-host-project");
  assert.equal(member.msgs.find((m) => m.t === "welcome").compatibility.mixed, true);
  await host.wait((m) => m.t === "presence" && m.compatibility?.mixed);
  await refused("legacy", { t: "join", session: "01" });
  await refused("older", { t: "join", session: "01", compat: compat("25.2.3.5171") });
  await refused("bad-member", { t: "join", session: "01", compat: { ...compat("26.1.7.5654"), protocol: 2 } });

  host.msgs.length = member.msgs.length = 0;
  member.json({ t: "op", source_fl: "25.2.5.5319", op: { kind: "param.set", v: 9 } });
  assert.equal((await host.wait((m) => m.t === "op")).source_fl, "26.1.7.5654");
  host.msgs.length = member.msgs.length = 0;
  member.json({ t: "op", op: { kind: "plug.state", data: "new-format" } });
  assert.equal((await member.wait((m) => m.t === "error")).code, "compatibility");
  host.json({ t: "op", op: { kind: "plug.state", data: "older-format" } });
  assert.equal((await member.wait((m) => m.t === "op")).source_fl, "25.2.5.5319");
  assert.equal(host.msgs.some((m) => m.t === "op"), false);
  member.msgs.length = 0;
  member.json({ t: "op", op: { kind: "param.set", data: [{ source_fl: "25.2.5.5319" }] } });
  assert.equal((await member.wait((m) => m.t === "error")).code, "bad_request");

  const newerHost = await connect("newer-host", { t: "host", compat: compat("26.1.7.5654") });
  const newerSlot = newerHost.msgs.find((m) => m.t === "welcome").session;
  await refused("old-again", { t: "join", session: newerSlot, compat: compat("25.2.5.5319") });
  const legacyHost = await connect("legacy-host", { t: "host" });
  const legacySlot = legacyHost.msgs.find((m) => m.t === "welcome").session;
  await refused("new-to-legacy", { t: "join", session: legacySlot, compat: compat("26.1.7.5654") });
  assert.equal(child.exitCode, null);
});
