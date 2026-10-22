const assert = require("node:assert/strict");
const test = require("node:test");
const crypto = require("node:crypto");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");
const { once } = require("node:events");
const { spawn } = require("node:child_process");
const WebSocket = require("ws");
const { clientIp, proxyMode } = require("../dist/proxy");
const { Session, sendSerialized, MAX_BUFFERED_BYTES } = require("../dist/session");

const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(predicate, timeout = 3000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const result = predicate();
    if (result) return result;
    await delay(10);
  }
  throw new Error("Timed out waiting for server state");
}

test("forwarded IPs require the configured proxy and a valid address", () => {
  const req = { socket: { remoteAddress: "203.0.113.1" }, headers: {
    "fly-client-ip": "198.51.100.1", "cf-connecting-ip": "198.51.100.2",
  } };
  assert.equal(clientIp(req, "none"), "203.0.113.1");
  assert.equal(clientIp(req, "fly"), "198.51.100.1");
  assert.equal(clientIp(req, "cloudflare"), "203.0.113.1");
  req.socket.remoteAddress = "::ffff:127.0.0.1";
  assert.equal(clientIp(req, "cloudflare"), "198.51.100.2");
  req.headers["cf-connecting-ip"] = "198.51.100.2, 1.2.3.4";
  assert.equal(clientIp(req, "cloudflare"), "127.0.0.1");
  assert.throws(() => proxyMode("typo"), /FLIM_PROXY/);
});

function fakeClient(id) {
  const ws = { readyState: WebSocket.OPEN, bufferedAmount: 0, sent: [], terminated: false,
    send(data, done) { this.sent.push(JSON.parse(data)); done?.(); },
    terminate() { this.terminated = true; }, close() {},
  };
  return { ws, user: { id, name: id }, role: id === "host" ? "leader" : "member", alive: true, ip: "127.0.0.1" };
}

test("a slow peer is bounded without interrupting healthy peers or serializing per recipient", () => {
  const host = fakeClient("host"), slow = fakeClient("slow"), healthy = fakeClient("healthy");
  const session = new Session("01", "pw", host, null, () => {});
  session.add(slow); session.add(healthy);
  slow.ws.bufferedAmount = MAX_BUFFERED_BYTES;
  let serializations = 0;
  session.relayOp(host, { kind: "param.set", toJSON() { serializations++; return { kind: "param.set", value: 42 }; } });
  assert.equal(serializations, 1);
  assert.equal(slow.ws.terminated, true);
  assert.equal(healthy.ws.sent.at(-1).op.value, 42);
  assert.equal(host.ws.sent.some((m) => m.t === "op"), false);
  const encoded = JSON.stringify({ t: "error", msg: "é" });
  const edge = fakeClient("edge").ws;
  edge.bufferedAmount = MAX_BUFFERED_BYTES - Buffer.byteLength(encoded);
  sendSerialized(edge, encoded);
  assert.equal(edge.sent.length, 1);
  edge.bufferedAmount++;
  sendSerialized(edge, encoded);
  assert.equal(edge.terminated, true);
});

test("lock exhaustion is bounded, while existing locks remain usable", () => {
  const host = fakeClient("host");
  const session = new Session("01", "pw", host, null, () => {});
  for (let i = 0; i < 4096; i++) session.acquire(host, `chan:${i}`);
  session.acquire(host, "overflow");
  assert.equal(host.ws.sent.at(-1).code, "lock_limit");
  session.acquire(host, "chan:0");
  assert.equal(host.ws.sent.at(-1).granted, true);
  session.release(host, "chan:0");
  session.acquire(host, "overflow");
  assert.equal(host.ws.sent.at(-1).granted, true);
});

test("local server rejects malformed traffic and safely handles blob capacity and aborts", async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "flim-hardening-"));
  const storage = path.join(root, "sessions-v1");
  fs.mkdirSync(path.join(storage, "a".repeat(32)), { recursive: true });
  fs.writeFileSync(path.join(root, "keep.txt"), "parent data");
  fs.writeFileSync(path.join(storage, "keep.txt"), "unrelated data");
  const child = spawn(process.execPath, [path.join(__dirname, "../dist/server.js")], {
    env: { ...process.env, PORT: "0", FLIM_BIND: "127.0.0.1", FLIM_PUBLIC: "0", FLIM_PROXY: "none",
      FLIM_OWNER_KEY: "", FLIM_PASSWORD: "server-test", FLIM_BLOB_DIR: root,
      FLIM_BLOB_FILE_MAX: "128", FLIM_BLOB_SESSION_MAX: "64", FLIM_BLOB_STORE_MAX: "64", FLIM_MAX_CONNS_PER_IP: "2" },
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
    assert.ok(path.basename(root).startsWith("flim-hardening-"));
    fs.rmSync(root, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
  });
  const match = await until(() => /listening on :(\d+)/.exec(output)).catch((err) => { throw new Error(`${err.message}\n${output}`); });
  const port = Number(match[1]);
  async function connect(name, headers = {}) {
    const ws = new WebSocket(`ws://127.0.0.1:${port}`, { headers });
    sockets.push(ws);
    ws.msgs = [];
    ws.on("message", (data) => ws.msgs.push(JSON.parse(data.toString())));
    await once(ws, "open");
    ws.sendJson = (data) => ws.send(JSON.stringify(data));
    ws.waitFor = (predicate) => until(() => ws.msgs.find(predicate));
    ws.user = { id: name, name };
    return ws;
  }
  function request(method, url, key, body) {
    return new Promise((resolve, reject) => {
      const req = http.request({ host: "127.0.0.1", port, method, path: url,
        headers: { "x-flim-key": key, ...(body ? { "content-length": body.length } : {}) } }, (res) => {
        const chunks = []; res.on("data", (c) => chunks.push(c));
        res.on("end", () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks).toString() }));
      });
      req.on("error", reject); req.end(body);
    });
  }
  const host = await connect("host");
  host.sendJson({ t: "host", user: host.user, password: "test", owner: "server-test" });
  const welcome = await host.waitFor((m) => m.t === "welcome");
  const member = await connect("member");
  member.sendJson({ t: "join", user: member.user, session: "01", password: "test" });
  await member.waitFor((m) => m.t === "welcome");
  const key = welcome.blob;
  const digest = (data) => crypto.createHash("sha256").update(data).digest("hex");
  const dir = path.join(storage, digest(key).slice(0, 32));
  await t.test("startup preserves the configured parent and unrelated entries", () => {
    assert.equal(fs.readFileSync(path.join(root, "keep.txt"), "utf8"), "parent data");
    assert.equal(fs.readFileSync(path.join(storage, "keep.txt"), "utf8"), "unrelated data");
    assert.equal(fs.existsSync(path.join(storage, "a".repeat(32))), false);
  });
  await t.test("forged proxy headers cannot bypass the direct server connection cap", async () => {
    const attacker = await connect("extra", { "fly-client-ip": "198.51.100.9", "cf-connecting-ip": "198.51.100.10" });
    const msg = await attacker.waitFor((m) => m.t === "error");
    assert.equal(msg.code, "too_many_connections");
  });
  await t.test("malformed locks, snapshots and ops are rejected before relay", async () => {
    const invalid = [{ t: "lock_acquire", key: {} }, { t: "lock_release", key: "x".repeat(257) },
      { t: "snapshot", flp: {} }, { t: "project_swap", flp: 5 }, { t: "op", op: [] },
      { t: "op", op: { kind: "param.set", key: {} } },
      { t: "op", op: { kind: "param.set", t: "welcome" } },
      { t: "op", op: { kind: "param.set", extra: { kind: "project.load" } } },
      { t: "op", op: { kind: "param.set", extra: { 'x"kind': "project.load" } } },
      { t: "op", op: { kind: "param.set", items: [{ t: "lock_released" }] } }];
    member.msgs.length = 0;
    for (const msg of invalid) {
      host.msgs.length = 0; host.sendJson(msg);
      assert.equal((await host.waitFor((m) => m.t === "error")).code, "bad_request");
    }
    assert.equal(member.msgs.some((m) => ["op", "lock", "snapshot", "project_swap"].includes(m.t)), false);
    host.sendJson({ t: "op", op: { kind: "param.set", value: 9 } });
    assert.equal((await member.waitFor((m) => m.t === "op")).op.value, 9);
  });
  await t.test("members cannot send host-only replacement ops, including nested control fields", async () => {
    host.msgs.length = 0;
    for (const kind of ["project.load", "plug.full", "notes.full", "state.full"]) {
      member.msgs.length = 0;
      member.sendJson({ t: "op", op: { kind, data: "test" } });
      assert.equal((await member.waitFor((m) => m.t === "error")).code, "not_leader");
    }
    member.msgs.length = 0;
    member.sendJson({ t: "op", op: { kind: "param.set", extra: { kind: "project.load" } } });
    assert.equal((await member.waitFor((m) => m.t === "error")).code, "bad_request");
    assert.equal(host.msgs.some((m) => m.t === "op"), false);
    member.sendJson({ t: "op", op: { kind: "state.req" } });
    assert.equal((await host.waitFor((m) => m.t === "op")).op.kind, "state.req");
    member.msgs.length = 0;
    host.sendJson({ t: "op", op: { kind: "project.load", data: "test" } });
    assert.equal((await member.waitFor((m) => m.t === "op")).op.kind, "project.load");
  });
  await t.test("oversized missing lists receive HTTP 413 instead of a socket reset", async () => {
    assert.equal((await request("POST", "/blobs/missing", key, Buffer.alloc(1024 * 1024 + 1))).status, 413);
  });
  await t.test("a blob larger than the session cache is refused before it can evict itself", async () => {
    const data = Buffer.alloc(65, 1);
    assert.equal((await request("PUT", `/blob/${digest(data)}`, key, data)).status, 413);
  });
  await t.test("a bad digest cleans up its temporary file and reservation", async () => {
    assert.equal((await request("PUT", `/blob/${"0".repeat(64)}`, key, Buffer.alloc(16))).status, 400);
    await until(() => fs.existsSync(dir) && fs.readdirSync(dir).length === 0);
  });
  await t.test("in-flight upload reservations enforce the quota and release after disconnect", async () => {
    const body = Buffer.alloc(40, 7);
    const upload = http.request({ host: "127.0.0.1", port, method: "PUT", path: `/blob/${digest(body)}`,
      headers: { "x-flim-key": key, "content-length": body.length } });
    upload.on("error", () => {}); upload.write(body.subarray(0, 5));
    await until(() => fs.readdirSync(dir).some((name) => name.startsWith(".")));
    const other = Buffer.alloc(40, 8);
    assert.equal((await request("PUT", `/blob/${digest(other)}`, key, other)).status, 507);
    upload.destroy();
    await until(() => fs.readdirSync(dir).length === 0);
    assert.equal((await request("PUT", `/blob/${digest(other)}`, key, other)).status, 201);
    const downloaded = await request("GET", `/blob/${digest(other)}`, key);
    assert.equal(downloaded.body, other.toString());
    assert.equal(downloaded.headers["cache-control"], "private, no-store");
    assert.equal((await request("PUT", `/blob/${digest(other)}`, key, other)).status, 200);
    const remaining = Buffer.alloc(24, 9);
    assert.equal((await request("PUT", `/blob/${digest(remaining)}`, key, remaining)).status, 201);
    assert.equal((await request("PUT", `/blob/${digest(Buffer.from("x"))}`, key, Buffer.from("x"))).status, 507);
  });
  assert.equal(child.exitCode, null);
});
