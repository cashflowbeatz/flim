// CPU-only comparison against the original per-recipient JSON.stringify loop. No network or FL.
const { performance } = require("node:perf_hooks");
const os = require("node:os");
const { Session } = require("../dist/session");
const { WebSocket } = require("ws");

let bytesWritten = 0;
function client(id, role) {
  return { user: { id, name: id }, role, ip: "127.0.0.1", alive: true,
    ws: { readyState: WebSocket.OPEN, bufferedAmount: 0,
      send(data, callback) { bytesWritten += Buffer.byteLength(data); callback?.(); },
      terminate() { throw new Error("Benchmark should not hit backpressure"); } },
  };
}
const host = client("host", "leader");
const peers = Array.from({ length: 7 }, (_, i) => client(`peer${i}`, "member"));
const session = new Session("01", "pw", host, null, () => {});
peers.forEach((peer) => session.add(peer));
let seq = 0;
function original(op) {
  const msg = { t: "op", seq: ++seq, from: "host", op };
  for (const peer of peers) if (peer.ws.readyState === WebSocket.OPEN) peer.ws.send(JSON.stringify(msg));
}
const median = (values) => values.sort((a, b) => a - b)[Math.floor(values.length / 2)];
function elapsed(fn, op, iterations) {
  const start = performance.now();
  for (let i = 0; i < iterations; i++) fn(op);
  return performance.now() - start;
}
console.log(`Node ${process.version}, ${os.platform()} ${os.arch()}, ${os.cpus()[0].model}`);
console.log("Seven recipients; median of five runs; byte accounting included, no sockets.");
for (const [payloadBytes, iterations] of [[4096, 500], [4 * 1024 * 1024, 10]]) {
  const op = { kind: "plug.state", data: "x".repeat(payloadBytes) };
  const optimized = (value) => session.relayOp(host, value);
  elapsed(original, op, 2); elapsed(optimized, op, 2);
  const before = [], after = [];
  for (let i = 0; i < 5; i++) {
    before.push(elapsed(original, op, iterations));
    after.push(elapsed(optimized, op, iterations));
  }
  const oldMs = median(before), newMs = median(after);
  console.log(JSON.stringify({ payloadBytes, iterations, originalMs: +oldMs.toFixed(2),
    currentMs: +newMs.toFixed(2), ratio: +(oldMs / newMs).toFixed(2) }));
}
if (!bytesWritten) throw new Error("No messages were delivered");
