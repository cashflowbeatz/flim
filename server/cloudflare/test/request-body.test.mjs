import { test } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { transform } from "esbuild";

const source = await readFile(new URL("../src/request-body.ts", import.meta.url), "utf8");
const { code } = await transform(source, { loader: "ts", format: "esm", target: "es2022" });
const { boundedText } = await import("data:text/javascript;base64," + Buffer.from(code).toString("base64"));
const request = body => new Request("https://flim/rooms", { method: "POST", body, duplex: "half" });

test("bounded request reads preserve split Unicode and enforce actual byte size", async () => {
  const data = new TextEncoder().encode("a🎹b");
  const chunks = new ReadableStream({ start(c) { c.enqueue(data.slice(0, 3)); c.enqueue(data.slice(3)); c.close(); } });
  assert.equal(await boundedText(request(chunks), data.length), "a🎹b");
  await assert.rejects(boundedText(request("🎹"), 3), { status: 413 });
  const over = new Request("https://flim/rooms", { method: "POST", headers: { "content-length": "999" }, body: "a" });
  await assert.rejects(boundedText(over, 10), { status: 413 });
});

test("stalled body times out even if its source never finishes cancellation", async () => {
  let canceled = false;
  const body = new ReadableStream({
    start(c) { c.enqueue(new TextEncoder().encode("partial")); },
    cancel() { canceled = true; return new Promise(() => {}); }
  });
  await assert.rejects(boundedText(request(body), 1024, 20), { status: 408 });
  assert.equal(canceled, true);
  assert.equal(body.locked, false);
  assert.equal(await boundedText(request("complete"), 1024), "complete");
});

test("broken body streams return a client error and release their reader", async () => {
  const body = new ReadableStream({ pull(c) { c.error(new Error("Disconnected")); } });
  await assert.rejects(boundedText(request(body), 1024), { status: 400 });
  assert.equal(body.locked, false);
});
