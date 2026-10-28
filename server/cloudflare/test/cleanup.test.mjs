import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { Miniflare } from "miniflare";

test("failed R2 deletion keeps its storage reservation until cleanup succeeds", async t => {
  const owner = "local-test-host-key-never-deploy-this";
  const body = "committed-but-unconfirmed";
  const hash = createHash("sha256").update(body).digest("hex");
  const mf = new Miniflare({
    modules: true, scriptPath: fileURLToPath(new URL("cleanup-fixture.mjs", import.meta.url)),
    compatibilityDate: "2026-07-30",
    durableObjects: { ROOM: { className: "CleanupTestRoom", useSQLite: true } },
    r2Buckets: ["FILES"], bindings: { HOST_KEY: owner, R2_STORAGE_LIMIT_BYTES: String(body.length) }
  });
  t.after(() => mf.dispose());
  const response = await mf.dispatchFetch("http://flim/", { headers: { Upgrade: "websocket" } });
  assert.equal(response.status, 101);
  const ws = response.webSocket;
  ws.accept();
  const welcomed = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error("Host welcome did not arrive")), 5000);
    ws.addEventListener("message", e => {
      const message = JSON.parse(e.data);
      if (message.t === "welcome") { clearTimeout(timeout); resolve(message); }
    });
  });
  ws.send(JSON.stringify({ t: "host", transport: 2, user: { id: "host", name: "host" },
    owner, password: "room-password", compat: { protocol: 1, fl: "25.2.5.5319", wire: "flim-25-26-v1" } }));
  const welcome = await welcomed;
  const namespace = await mf.getDurableObjectNamespace("ROOM");
  const room = namespace.get(namespace.idFromName("private-room-01"));
  const bucket = await mf.getR2Bucket("FILES");
  const usage = async () => (await mf.dispatchFetch("http://flim/usage", { headers: { "x-flim-owner": owner } })).json();
  const put = text => mf.dispatchFetch("http://flim/blob/" + createHash("sha256").update(text).digest("hex"), {
    method: "PUT", headers: { "x-flim-key": welcome.blob, "content-length": String(Buffer.byteLength(text)) }, body: text
  });

  await room.testFailUploadAndDeletes();
  assert.equal((await put(body)).status, 400);
  const objects = (await bucket.list()).objects;
  assert.equal(objects.length, 1);
  assert.ok(objects[0].key.endsWith("/" + hash));
  assert.equal(await (await bucket.get(objects[0].key)).text(), body);
  let state = await usage();
  assert.equal(state.storage.bytes, body.length);
  assert.equal(state.storage.files, 1);
  assert.equal(state.uploads.used, 1);
  assert.equal((await put("overflow")).status, 507);

  assert.equal(await room.testCleanup(), "Injected R2 cleanup failure");
  const retryAt = await room.testAlarmTime();
  assert.ok(retryAt > Date.now() && retryAt <= Date.now() + 61000, 'Cleanup failures must leave another alarm scheduled');
  state = await usage();
  assert.equal(state.storage.bytes, body.length);
  assert.equal(state.storage.files, 1);
  assert.equal((await bucket.list()).objects.length, 1);

  assert.equal(await room.testCleanup(), null);
  state = await usage();
  assert.equal(state.storage.bytes, 0);
  assert.equal(state.storage.files, 0);
  assert.equal(state.uploads.used, 1);
  assert.equal((await bucket.list()).objects.length, 0);
  assert.equal((await put("fits-again")).status, 201);
});
