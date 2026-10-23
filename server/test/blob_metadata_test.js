const assert = require("node:assert/strict");
const test = require("node:test");
const fs = require("node:fs");
const http = require("node:http");
const crypto = require("node:crypto");
const os = require("node:os");
const path = require("node:path");
const { once } = require("node:events");

test("sample metadata queries deduplicate disk checks and recheck expired sessions", async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "flim-blob-metadata-"));
  process.env.FLIM_BLOB_DIR = root;
  const { handleBlob, blobDir } = require("../dist/blobs");
  const key = "2".repeat(64);
  let active = true;
  const server = http.createServer((req, res) => handleBlob(req, res, (given) => active && given === key));
  const requests = [];
  const statSync = fs.statSync;
  t.after(async () => {
    fs.statSync = statSync;
    requests.forEach((req) => req.destroy());
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
    assert.equal(path.dirname(path.resolve(root)), path.resolve(os.tmpdir()));
    assert.ok(path.basename(root).startsWith("flim-blob-metadata-"));
    fs.rmSync(root, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
  });
  server.listen(0, "127.0.0.1"); await once(server, "listening");
  const port = server.address().port;
  const digest = (data) => crypto.createHash("sha256").update(data).digest("hex");
  function request(method, url, length) {
    let req;
    const response = new Promise((resolve, reject) => {
      req = http.request({ host: "127.0.0.1", port, method, path: url,
        headers: { "x-flim-key": key, "content-length": length } }, (res) => {
        const chunks = []; res.on("data", (chunk) => chunks.push(chunk));
        res.on("end", () => resolve({ status: res.statusCode, body: Buffer.concat(chunks).toString() }));
      });
      req.on("error", reject);
    });
    requests.push(req);
    return { req, response };
  }
  const data = Buffer.from("sample"), stored = digest(data), missing = digest(Buffer.from("missing"));
  const upload = request("PUT", `/blob/${stored}`, data.length); upload.req.end(data);
  assert.equal((await upload.response).status, 201);
  const lookups = [];
  fs.statSync = (file, ...args) => {
    if (path.dirname(String(file)) === blobDir(key)) lookups.push(path.basename(file));
    return statSync(file, ...args);
  };
  await t.test("repeated sample hashes are looked up once and returned once", async () => {
    const body = Buffer.from([stored, missing, stored, missing, missing, "invalid", stored.toUpperCase()].join("\n"));
    const query = request("POST", "/blobs/missing", body.length); query.req.end(body);
    const result = await query.response;
    assert.equal(result.status, 200);
    assert.equal(result.body, missing);
    assert.deepEqual(lookups, [stored, missing]);
  });
  await t.test("a session ending during request upload cannot query deleted storage", async () => {
    const body = Buffer.from(missing + "\n");
    const query = request("POST", "/blobs/missing", body.length);
    const incoming = once(server, "request");
    query.req.write(body.subarray(0, 10));
    await incoming;
    active = false;
    lookups.length = 0;
    query.req.end(body.subarray(10));
    const result = await query.response;
    assert.equal(result.status, 410);
    assert.equal(result.body, "");
    assert.deepEqual(lookups, []);
  });
});
