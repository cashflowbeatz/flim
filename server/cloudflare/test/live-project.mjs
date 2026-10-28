import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";

const relay = process.env.FLIM_TEST_RELAY ?? "ws://127.0.0.1:49938/";
const ws = new WebSocket(relay); let key;
const timeout = setTimeout(() => { console.error("Project resync timed out"); process.exit(1); }, 30000);
await new Promise((resolve, reject) => {
  ws.addEventListener("open", () => ws.send(JSON.stringify({ t: "join", session: process.env.FLIM_TEST_ROOM ?? "01", password: process.env.FLIM_TEST_PASSWORD ?? "local-version-validation", transport: 2,
    user: { id: "test-" + randomUUID(), name: "Project test" }, compat: { protocol: 1, fl: process.env.FLIM_TEST_FL ?? "26.1.7.5654", wire: "flim-25-26-v1" } })));
  ws.addEventListener("error", reject);
  ws.addEventListener("message", async event => {
    try {
      let message = JSON.parse(event.data);
      if (message.t === "error") throw new Error(message.code + ": " + message.msg);
      if (message.t === "welcome") { key = message.blob; ws.send(JSON.stringify({ t: "op", op: { kind: "state.req" } })); }
      const stored = message.t === "stored" && message.kind === "project.load";
      if (stored) {
        const url = new URL("/blob/" + message.h, relay.replace(/^ws/, "http"));
        const response = await fetch(url, { headers: { "x-flim-key": key } }); assert.equal(response.status, 200);
        const bytes = Buffer.from(await response.arrayBuffer()); assert.equal(bytes.length, message.s);
        assert.equal(createHash("sha256").update(bytes).digest("hex"), message.h);
        message = JSON.parse(bytes.toString("utf8"));
      }
      if (message.t === "op" && message.op.kind === "project.load") {
        assert.ok(Buffer.from(message.op.data, "base64").length > 0);
        console.log(JSON.stringify({ project: "received", stored, serializedBytes: Buffer.byteLength(JSON.stringify(message)) }));
        resolve();
      }
    } catch (error) { reject(error); }
  });
}).finally(() => { clearTimeout(timeout); ws.close(1000, "test complete"); });
