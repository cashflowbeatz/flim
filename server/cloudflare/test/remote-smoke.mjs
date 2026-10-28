import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";

const relay = new URL(process.env.FLIM_TEST_RELAY ?? "https://localhost/");
assert.ok(["https:", "http:"].includes(relay.protocol), "FLIM_TEST_RELAY must be an HTTP(S) URL");
const hostKey = (await readFile(process.env.FLIM_TEST_HOST_KEY_FILE, "utf8")).trim();
assert.ok(hostKey.length >= 24, "Provide the private hosting-key file");
const wsUrl = new URL(relay); wsUrl.protocol = relay.protocol === "https:" ? "wss:" : "ws:";
const password = randomUUID(), peers = [], results = [];
const sha = bytes => createHash("sha256").update(bytes).digest("hex");
const compat = fl => ({ protocol: 1, fl, wire: "flim-25-26-v1" });
const hello = (t, fl, extra = {}) => ({ t, transport: 2, session: "01", password,
  user: { id: randomUUID(), name: "Deployment test" }, compat: compat(fl), ...extra });
const passed = name => { results.push(name); console.log("PASS " + name); };
async function open(message) {
  const ws = new WebSocket(wsUrl), queue = []; let wake;
  const peer = { ws, send: value => ws.send(JSON.stringify(value)), async next(type) {
    const deadline = Date.now() + 20000;
    for (;;) {
      const index = queue.findIndex(m => m.t === type || m.t === "error");
      if (index >= 0) { const [m] = queue.splice(index, 1); assert.equal(m.t, type, "Unexpected relay error: " + m.code); return m; }
      assert.ok(Date.now() < deadline, "Timed out waiting for " + type);
      await new Promise(resolve => { const timer = setTimeout(resolve, Math.max(1, deadline - Date.now())); wake = () => { clearTimeout(timer); resolve(); }; });
      wake = undefined;
    }
  } };
  peers.push(peer);
  ws.addEventListener("message", event => { queue.push(JSON.parse(event.data)); wake?.(); });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("WebSocket connection timed out")), 20000);
    ws.addEventListener("open", () => { clearTimeout(timer); resolve(); }, { once: true });
    ws.addEventListener("error", () => { clearTimeout(timer); reject(new Error("WebSocket connection failed")); }, { once: true });
  });
  peer.send(message); return peer;
}
async function file(key, hash, method = "GET", body) {
  return fetch(new URL("/blob/" + hash, relay), { method,
    headers: { "x-flim-key": key, ...(body !== undefined ? { "content-length": String(Buffer.byteLength(body)) } : {}) },
    ...(body !== undefined ? { body } : {}), signal: AbortSignal.timeout(20000) });
}
try {
  const health = await fetch(new URL("/health", relay), { signal: AbortSignal.timeout(20000) });
  assert.equal(health.status, 200); assert.equal((await health.text()).trim(), "flim-server ok"); passed("public health endpoint");
  assert.equal((await fetch(new URL("/usage", relay), { signal: AbortSignal.timeout(20000) })).status, 403);
  const usageRequest = () => fetch(new URL("/usage", relay), { headers: { "x-flim-owner": hostKey }, signal: AbortSignal.timeout(20000), redirect: "error" }).then(async response => { assert.equal(response.status, 200); return response.json(); });
  const usageBefore = await usageRequest();
  assert.equal(usageBefore.storage.limit_bytes, 536870912); assert.equal(usageBefore.uploads.limit, 20000); assert.equal(usageBefore.downloads.limit, 100000);
  passed("private usage endpoint exposes deployed conservative cutoffs");
  const bad = await open(hello("host", "25.2.5.5319", { owner: "incorrect-deployment-test-key" }));
  assert.equal((await bad.next("error")).code, "bad_owner"); passed("incorrect hosting key rejected");
  const host = await open(hello("host", "25.2.5.5319", { owner: hostKey }));
  const welcome = await host.next("welcome");
  assert.equal(welcome.transport, 2); assert.equal(welcome.ends_in, 0); passed("host admitted without session countdown");
  const badPassword = await open(hello("join", "26.1.7.5654", { password: randomUUID() }));
  assert.equal((await badPassword.next("error")).code, "bad_password"); passed("incorrect room password rejected");
  const member = await open(hello("join", "26.1.7.5654"));
  const memberWelcome = await member.next("welcome"); assert.equal(memberWelcome.compatibility.mixed, true); passed("mixed FL 25/26 room admitted");
  member.send({ t: "op", op: { kind: "notes.set", value: 1 } });
  const edit = await host.next("op"); assert.equal(edit.source_fl, "26.1.7.5654"); assert.ok(edit.seq > 0); passed("ordered edits retain sender build");
  member.send({ t: "op", op: { kind: "project.load", data: "" } });
  assert.equal((await member.next("error")).code, "compatibility"); passed("mixed-build project replacement restriction");
  const body = JSON.stringify({ t: "op", op: { kind: "project.load", data: "A".repeat(180000) } }), hash = sha(body);
  assert.equal((await file("incorrect-file-key", hash, "PUT", body)).status, 403); passed("private file access enforced");
  assert.equal((await file(welcome.blob, sha("mismatch"), "PUT", body)).status, 400); passed("incorrect file hash rejected");
  assert.equal((await file(welcome.blob, hash, "PUT", body)).status, 201);
  const missing = sha("missing-deployment-test");
  const response = await fetch(new URL("/blobs/missing", relay), { method: "POST", headers: { "x-flim-key": welcome.blob }, body: hash + "\n" + missing + "\n", signal: AbortSignal.timeout(20000) });
  assert.equal(response.status, 200); assert.equal(await response.text(), missing); passed("native missing-file line protocol");
  host.send({ t: "stored", type: "op", kind: "project.load", h: hash, s: Buffer.byteLength(body) });
  const ref = await member.next("stored"); assert.equal(ref.h, hash); assert.equal(ref.source_fl, "25.2.5.5319");
  const download = await file(memberWelcome.blob, hash); assert.equal(download.status, 200);
  const bytes = Buffer.from(await download.arrayBuffer()); assert.equal(bytes.length, ref.s); assert.equal(sha(bytes), hash); passed("large edit uploaded and downloaded through private R2");
  host.ws.close(1000, "deployment test complete"); await member.next("leader_left");
  assert.equal((await file(welcome.blob, hash)).status, 403); passed("host departure ends session and revokes file access");
  const usageAfter = await usageRequest();
  assert.equal(usageAfter.uploads.used - usageBefore.uploads.used, 2); assert.equal(usageAfter.downloads.used - usageBefore.downloads.used, 1);
  passed("deployed R2 attempts counted including failed checksum after session end");
  console.log(JSON.stringify({ endpoint: relay.origin, passed: results.length, checks: results }));
} finally {
  await Promise.all(peers.map(({ ws }) => new Promise(resolve => {
    if (ws.readyState === WebSocket.CLOSED) return resolve();
    const timer = setTimeout(resolve, 3000);
    ws.addEventListener("close", () => { clearTimeout(timer); resolve(); }, { once: true });
    ws.close(1000, "deployment test finished");
  })));
}
