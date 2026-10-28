import { Miniflare } from "miniflare";
import { fileURLToPath } from "node:url";
const mf = new Miniflare({
  modules: true, scriptPath: fileURLToPath(new URL("../dist/worker.mjs", import.meta.url)),
  host: "127.0.0.1", port: 49938, compatibilityDate: "2026-07-30",
  durableObjects: { ROOM: { className: "FlimRoom", useSQLite: true } }, r2Buckets: ["FILES"],
  bindings: { HOST_KEY: "local-test-host-key-never-deploy-this" }
});
console.log("Local Cloudflare runtime:", String(await mf.ready));
let closing = false;
const close = async () => { if (closing) return; closing = true; await mf.dispose(); process.exit(0); };
process.on("SIGINT", close); process.on("SIGTERM", close);
process.stdin.on("data", data => { if (String(data).trim() === "stop") void close(); });
process.stdin.resume();
