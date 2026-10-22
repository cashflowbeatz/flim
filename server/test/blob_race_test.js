const assert = require("node:assert/strict");
const test = require("node:test");
const fs = require("node:fs");
const http = require("node:http");
const crypto = require("node:crypto");
const os = require("node:os");
const path = require("node:path");
const { once } = require("node:events");
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

test("an aborted duplicate after rename preserves the committed blob and releases its reservation", async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "flim-blob-race-"));
  process.env.FLIM_BLOB_DIR = root;
  process.env.FLIM_BLOB_FILE_MAX = "64";
  process.env.FLIM_BLOB_SESSION_MAX = "64";
  process.env.FLIM_BLOB_STORE_MAX = "64";
  const { handleBlob, blobDir } = require("../dist/blobs");
  const key = "1".repeat(64);
  const server = http.createServer((req, res) => handleBlob(req, res, (given) => given === key));
  const originalRename = fs.rename;
  let calls = 0, finishRename, signalRenamed;
  const renamed = new Promise((resolve) => signalRenamed = resolve);
  fs.rename = (from, to, callback) => {
    const current = ++calls;
    originalRename(from, to, (err) => {
      if (current === 2) { finishRename = () => callback(err); signalRenamed(); }
      else callback(err);
    });
  };
  const requests = [];
  t.after(async () => {
    fs.rename = originalRename;
    requests.forEach((req) => req.destroy());
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
    assert.equal(path.dirname(path.resolve(root)), path.resolve(os.tmpdir()));
    assert.ok(path.basename(root).startsWith("flim-blob-race-"));
    fs.rmSync(root, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
  });
  server.listen(0, "127.0.0.1"); await once(server, "listening");
  const port = server.address().port;
  const digest = (data) => crypto.createHash("sha256").update(data).digest("hex");
  function request(method, hex, length) {
    let req;
    const response = new Promise((resolve, reject) => {
      req = http.request({ host: "127.0.0.1", port, method, path: `/blob/${hex}`,
        headers: { "x-flim-key": key, ...(length === undefined ? {} : { "content-length": length }) } }, (res) => {
        const chunks = []; res.on("data", (data) => chunks.push(data));
        res.on("end", () => resolve({ status: res.statusCode, body: Buffer.concat(chunks) }));
      });
      req.on("error", reject);
    });
    requests.push(req);
    return { req, response };
  }
  const data = Buffer.alloc(16, 7), hash = digest(data);
  const duplicate = request("PUT", hash, data.length);
  duplicate.response.catch(() => {});
  duplicate.req.write(data.subarray(0, 5));
  for (let i = 0; i < 200 && !fs.existsSync(blobDir(key)); i++) await delay(5);
  assert.ok(fs.existsSync(blobDir(key)), "partial duplicate reached the store before the first commit");
  const first = request("PUT", hash, data.length); first.req.end(data);
  assert.equal((await first.response).status, 201);
  duplicate.req.end(data.subarray(5));
  let timer;
  try {
    await Promise.race([renamed, new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error("second rename did not happen")), 3000);
    })]);
  } finally { clearTimeout(timer); }
  const closed = new Promise((resolve) => duplicate.req.once("close", resolve));
  duplicate.req.destroy(); await closed;
  await delay(20);
  finishRename();
  await delay(20);
  const download = request("GET", hash); download.req.end();
  const result = await download.response;
  assert.equal(result.status, 200);
  assert.deepEqual(result.body, data);
  const remaining = Buffer.alloc(48, 8);
  const next = request("PUT", digest(remaining), remaining.length); next.req.end(remaining);
  assert.equal((await next.response).status, 201);
});
