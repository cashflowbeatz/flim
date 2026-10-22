const assert = require("node:assert/strict");
const test = require("node:test");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawn } = require("node:child_process");
const { once } = require("node:events");

for (const [name, value] of [
  ["FLIM_BLOB_STORE_MAX", "not-a-number"], ["FLIM_BLOB_FILE_MAX", "Infinity"],
  ["FLIM_BLOB_SESSION_MAX", "-1"], ["FLIM_MAX_CONNS_PER_IP", ""],
  ["FLIM_IDLE_MS", "0.5"], ["FLIM_SESSION_MS", "9007199254740992"],
  ["FLIM_COOLDOWN_MS", "-1"], ["PORT", "65536"],
]) {
  test(`startup refuses invalid ${name} before touching the sample cache`, async (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "flim-config-"));
    const cache = path.join(root, "sessions-v1", "a".repeat(32));
    fs.mkdirSync(cache, { recursive: true });
    const sentinel = path.join(cache, "keep.txt");
    fs.writeFileSync(sentinel, "existing cache");
    const child = spawn(process.execPath, [path.join(__dirname, "../dist/server.js")], {
      env: { ...process.env, PORT: "0", FLIM_BIND: "127.0.0.1", FLIM_PROXY: "none", FLIM_PUBLIC: "1",
        FLIM_PASSWORD: "test", FLIM_BLOB_DIR: root, FLIM_BLOB_STORE_MAX: "1024", FLIM_BLOB_FILE_MAX: "64",
        FLIM_BLOB_SESSION_MAX: "128", FLIM_MAX_CONNS_PER_IP: "12", FLIM_IDLE_MS: "0",
        FLIM_SESSION_MS: "0", FLIM_COOLDOWN_MS: "0", [name]: value },
      stdio: ["ignore", "pipe", "pipe"], windowsHide: true,
    });
    const exited = once(child, "exit");
    t.after(async () => {
      if (child.exitCode === null && child.signalCode === null) { child.kill(); await exited; }
      assert.equal(path.dirname(path.resolve(root)), path.resolve(os.tmpdir()));
      assert.ok(path.basename(root).startsWith("flim-config-"));
      fs.rmSync(root, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
    });
    let output = "", timer;
    const listening = new Promise((resolve) => {
      const read = (part) => { output += part; if (/listening on :/.test(output)) resolve("listening"); };
      child.stdout.on("data", read); child.stderr.on("data", read);
    });
    let result;
    try {
      result = await Promise.race([exited, listening, new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error(`Startup did not finish: ${output}`)), 5000);
      })]);
    } finally { clearTimeout(timer); }
    assert.notEqual(result, "listening", output);
    assert.notEqual(result[0], 0, output);
    assert.match(output, new RegExp(`${name} must be`));
    assert.equal(fs.readFileSync(sentinel, "utf8"), "existing cache");
  });
}
