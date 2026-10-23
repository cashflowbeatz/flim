const assert = require("node:assert/strict");
const test = require("node:test");
const fs = require("node:fs");
const http = require("node:http");
const crypto = require("node:crypto");
const os = require("node:os");
const path = require("node:path");
const { once } = require("node:events");
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(predicate) {
  for (let i = 0; i < 300; i++) {
    const value = predicate();
    if (value) return value;
    await delay(10);
  }
  throw new Error("Timed out waiting for transfer state");
}

test("sample transfers remain usable across eviction, open races and disconnects", { timeout: 15000 }, async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "flim-blob-lifecycle-"));
  process.env.FLIM_BLOB_DIR = root;
  process.env.FLIM_BLOB_FILE_MAX = "64";
  process.env.FLIM_BLOB_SESSION_MAX = "64";
  process.env.FLIM_BLOB_STORE_MAX = "1024";
  const { handleBlob, blobDir } = require("../dist/blobs");
  const originalOpen = fs.open, originalClose = fs.close, originalRename = fs.rename, originalStat = fs.fstat;
  const key = "3".repeat(64);
  let active = true;
  const server = http.createServer((req, res) => handleBlob(req, res, (given) => active && given === key));
  const requests = [];
  t.after(async () => {
    fs.open = originalOpen; fs.close = originalClose; fs.rename = originalRename; fs.fstat = originalStat;
    requests.forEach((req) => req.destroy());
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
    assert.equal(path.dirname(path.resolve(root)), path.resolve(os.tmpdir()));
    assert.ok(path.basename(root).startsWith("flim-blob-lifecycle-"));
    fs.rmSync(root, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
  });
  server.listen(0, "127.0.0.1"); await once(server, "listening");
  const port = server.address().port;
  const digest = (data) => crypto.createHash("sha256").update(data).digest("hex");
  function request(method, hex, length) {
    let req;
    const response = new Promise((resolve) => {
      req = http.request({ host: "127.0.0.1", port, method, path: `/blob/${hex}`,
        headers: { "x-flim-key": key, ...(length === undefined ? {} : { "content-length": length }) } }, (res) => {
        const chunks = []; res.on("data", (data) => chunks.push(data));
        res.on("end", () => resolve({ status: res.statusCode, body: Buffer.concat(chunks), headers: res.headers }));
        res.on("error", (err) => resolve({ status: res.statusCode, error: err.code }));
      });
      req.setTimeout(3000, () => req.destroy(new Error("Request timed out")));
      req.on("error", (err) => resolve({ status: null, error: err.code || err.message }));
    });
    requests.push(req);
    return { req, response };
  }
  async function upload(data) {
    const hex = digest(data), put = request("PUT", hex, data.length); put.req.end(data);
    assert.equal((await put.response).status, 201);
    return hex;
  }

  await t.test("a new commit survives eviction even if its temporary file has an older access time", async () => {
    const older = await upload(Buffer.alloc(32, 1));
    const data = Buffer.alloc(48, 2), hex = digest(data);
    const put = request("PUT", hex, data.length);
    put.req.write(data.subarray(0, 8));
    await until(() => fs.readdirSync(blobDir(key)).find((name) => name.startsWith(`.${hex}.`)));
    fs.utimesSync(path.join(blobDir(key), older), new Date(2000), new Date(2000));
    // Preserve the older access time of a slow upload across platform-specific close/rename updates.
    fs.rename = (from, to, callback) => originalRename(from, to, (err) => {
      if (!err && String(to) === path.join(blobDir(key), hex)) fs.utimesSync(to, new Date(1000), new Date(1000));
      callback(err);
    });
    try {
      put.req.end(data.subarray(8));
      assert.equal((await put.response).status, 201);
    } finally { fs.rename = originalRename; }
    const get = request("GET", hex); get.req.end();
    const result = await get.response;
    assert.equal(result.status, 200, "successful upload must still be available when the PUT returns");
    assert.deepEqual(result.body, data);
    assert.equal(fs.existsSync(path.join(blobDir(key), older)), false);
  });

  await t.test("a file removed before open returns a complete 404 response", async () => {
    const hex = await upload(Buffer.alloc(8, 3));
    fs.open = (file, ...args) => {
      if (String(file) === path.join(blobDir(key), hex)) fs.unlinkSync(file);
      return originalOpen(file, ...args);
    };
    try {
      const get = request("GET", hex); get.req.end();
      const result = await get.response;
      assert.equal(result.status, 404);
      assert.equal(result.error, undefined);
      assert.equal(result.body.length, 0);
    } finally { fs.open = originalOpen; }
  });

  await t.test("a session ending while a file opens returns 410 and closes the descriptor", async () => {
    const hex = await upload(Buffer.alloc(8, 4));
    let opened, closed = false;
    fs.open = (file, ...args) => {
      const callback = args.pop();
      return originalOpen(file, ...args, (err, fd) => {
        if (String(file) === path.join(blobDir(key), hex) && !err) { opened = fd; active = false; }
        callback(err, fd);
      });
    };
    fs.close = (fd, ...args) => { if (fd === opened) closed = true; return originalClose(fd, ...args); };
    try {
      const get = request("GET", hex); get.req.end();
      const result = await get.response;
      assert.equal(result.status, 410);
      await until(() => closed);
      assert.equal(result.body.length, 0);
    } finally { fs.open = originalOpen; fs.close = originalClose; active = true; }
  });

  await t.test("bytes already opened remain downloadable after their cache path is removed", async () => {
    const data = Buffer.alloc(8, 7), hex = await upload(data);
    fs.open = (file, ...args) => {
      const callback = args.pop();
      return originalOpen(file, ...args, (err, fd) => {
        if (String(file) === path.join(blobDir(key), hex) && !err) fs.unlinkSync(file);
        callback(err, fd);
      });
    };
    try {
      const get = request("GET", hex); get.req.end();
      const result = await get.response;
      assert.equal(result.status, 200);
      assert.deepEqual(result.body, data);
    } finally { fs.open = originalOpen; }
  });

  await t.test("a metadata read failure returns 500 and closes the opened descriptor", async () => {
    const hex = await upload(Buffer.alloc(8, 8));
    let opened, closed = false;
    fs.open = (file, ...args) => {
      const callback = args.pop();
      return originalOpen(file, ...args, (err, fd) => {
        if (String(file) === path.join(blobDir(key), hex) && !err) opened = fd;
        callback(err, fd);
      });
    };
    fs.fstat = (fd, callback) => {
      if (fd === opened) return setImmediate(() => callback(Object.assign(new Error("read failed"), { code: "EIO" })));
      return originalStat(fd, callback);
    };
    fs.close = (fd, ...args) => { if (fd === opened) closed = true; return originalClose(fd, ...args); };
    try {
      const get = request("GET", hex); get.req.end();
      const result = await get.response;
      assert.equal(result.status, 500);
      assert.equal(result.body.length, 0);
      await until(() => closed);
    } finally { fs.open = originalOpen; fs.close = originalClose; fs.fstat = originalStat; }
  });

  await t.test("a disconnected download closes a descriptor whose open callback was delayed", async () => {
    const hex = await upload(Buffer.alloc(8, 5));
    let release, opened, closed = false;
    fs.open = (file, ...args) => {
      const callback = args.pop();
      return originalOpen(file, ...args, (err, fd) => {
        if (String(file) === path.join(blobDir(key), hex) && !err) {
          opened = fd; release = () => callback(err, fd);
        } else callback(err, fd);
      });
    };
    fs.close = (fd, ...args) => { if (fd === opened) closed = true; return originalClose(fd, ...args); };
    try {
      const incoming = once(server, "request");
      const get = request("GET", hex); get.req.end();
      const [, res] = await incoming;
      await until(() => release);
      const responseClosed = once(res, "close");
      get.req.destroy(); await responseClosed;
      release();
      await until(() => closed);
    } finally { fs.open = originalOpen; fs.close = originalClose; }
  });

  await t.test("HEAD and GET describe the same stored bytes and preserve private responses", async () => {
    const data = Buffer.alloc(8, 6), hex = await upload(data);
    const head = request("HEAD", hex); head.req.end();
    const metadata = await head.response;
    assert.equal(metadata.status, 200);
    assert.equal(metadata.headers["content-length"], String(data.length));
    assert.equal(metadata.headers["cache-control"], "private, no-store");
    assert.equal(metadata.body.length, 0);
    const get = request("GET", hex); get.req.end();
    assert.deepEqual((await get.response).body, data);
  });
});
