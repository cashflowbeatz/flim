import { createHash, randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";

// Default probes stop before authentication. Room allocation requires an explicit opt-in.
// Set FLIM_COMPAT_SMOKE_ARTIFACT to test a built worker in an isolated local Miniflare.
const versions = ["24.2.2.4597", "25.2.5.5319", "26.1.7.5654"];
const compat = fl => ({ protocol: 1, fl, wire: "flim-25-26-v1" });
const createRoom = process.env.FLIM_COMPAT_SMOKE_CREATE === "yes";
const checks = [], sockets = [];
let base, runtime, artifactSha256, ownedRoom, hostWelcomed = false, phase = "configuration", roomReservationsUsed = 0;
class SmokeFailure extends Error {}
const requireCondition = (condition, message) => { if (!condition) throw new SmokeFailure(message); };
const check = (name, condition) => { requireCondition(condition, name); checks.push(name); };
const delay = ms => new Promise(resolveDelay => setTimeout(resolveDelay, ms));

function endpoint(path) {
  const url = new URL(path, base);
  requireCondition(url.origin === base.origin && !url.username && !url.password && !url.search && !url.hash, "Relay endpoint must stay on the selected origin");
  return url;
}

async function request(path, body, timeout = 10_000) {
  const response = await fetch(endpoint(path), {
    method: body === undefined ? "GET" : "POST", redirect: "error",
    ...(body === undefined ? {} : { headers: { "content-type": "application/json" }, body: JSON.stringify(body) }),
    signal: AbortSignal.timeout(timeout)
  });
  const text = await response.text();
  requireCondition(text.length <= 64 * 1024, "Relay response exceeded the smoke-test limit");
  return { status: response.status, text };
}

async function api(path, body, timeout) {
  const response = await request(path, body, timeout);
  try { return { status: response.status, body: JSON.parse(response.text) }; }
  catch { throw new SmokeFailure("Relay API did not return JSON"); }
}

async function open(path) {
  const url = endpoint(path);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(url), queue = [];
  let closed = false, fault = false, wake, pending = false;
  const notify = () => wake?.();
  const peer = {
    ws,
    send(value) {
      requireCondition(ws.readyState === WebSocket.OPEN, "Test socket was not open for sending");
      ws.send(JSON.stringify(value));
    },
    async next(type, predicate = () => true) {
      requireCondition(!pending, "Concurrent reads on one test socket are not supported");
      pending = true;
      const deadline = Date.now() + 10_000;
      try {
        for (;;) {
          requireCondition(!fault, "Test socket failed or sent invalid data");
          const error = queue.findIndex(message => message.t === "error");
          requireCondition(type === "error" || error < 0, "Relay rejected a test handshake or edit");
          const index = queue.findIndex(message => message.t === type && predicate(message));
          if (index >= 0) return queue.splice(index, 1)[0];
          requireCondition(!closed, "Test socket closed before its expected response");
          const remaining = deadline - Date.now();
          requireCondition(remaining > 0, "Test socket response timed out");
          await new Promise(resolveWait => {
            const done = () => { clearTimeout(timer); wake = undefined; resolveWait(); };
            const timer = setTimeout(done, remaining);
            wake = done;
          });
        }
      } finally { pending = false; wake = undefined; }
    },
    async close() {
      if (closed || ws.readyState === WebSocket.CLOSED) return;
      await new Promise(resolveClose => {
        const done = () => { clearTimeout(timer); ws.removeEventListener("close", done); resolveClose(); };
        const timer = setTimeout(done, 2_000);
        ws.addEventListener("close", done, { once: true });
        try { ws.close(1000, "compatibility validation complete"); } catch { done(); }
      });
    }
  };
  sockets.push(peer);
  ws.addEventListener("message", event => {
    try {
      requireCondition(typeof event.data === "string" && event.data.length <= 64 * 1024 && queue.length < 256, "Unexpected test socket payload");
      const message = JSON.parse(event.data);
      requireCondition(message && typeof message === "object" && !Array.isArray(message) && typeof message.t === "string", "Invalid test socket message");
      queue.push(message);
    } catch { fault = true; }
    notify();
  });
  ws.addEventListener("close", () => { closed = true; notify(); });
  ws.addEventListener("error", () => { fault = true; notify(); });
  await new Promise((resolveOpen, reject) => {
    const done = error => {
      clearTimeout(timer);
      ws.removeEventListener("open", onOpen); ws.removeEventListener("error", onError); ws.removeEventListener("close", onError);
      if (error) reject(error); else resolveOpen();
    };
    const onOpen = () => done();
    const onError = () => done(new SmokeFailure("Test socket could not open"));
    const timer = setTimeout(() => done(new SmokeFailure("Test socket opening timed out")), 10_000);
    ws.addEventListener("open", onOpen, { once: true });
    ws.addEventListener("error", onError, { once: true });
    ws.addEventListener("close", onError, { once: true });
  });
  return peer;
}

async function probe(fl, transport, expected, label) {
  const peer = await open("/");
  try {
    // An invalid user is rejected before room lookup, owner checks, or admission budgets.
    peer.send({ t: "host", transport, compat: compat(fl), user: { id: "", name: "Compatibility probe" }, password: "invalid-user-probe" });
    check(label, (await peer.next("error")).code === expected);
  } finally { await peer.close(); }
}

function validateRoom(room, hosting = false) {
  requireCondition(room && /^\d{8}$/.test(room.room) && /^[a-f0-9]{64}$/.test(room.password) &&
    room.websocket === `/room/${room.room}` && (!hosting || /^[a-f0-9]{64}$/.test(room.hostToken)), "Relay returned invalid room credentials");
}

async function connect(room, role, fl) {
  validateRoom(room, role === "host");
  const peer = await open(room.websocket), id = randomUUID();
  peer.send({ t: role, transport: 2, compat: compat(fl), user: { id, name: "Compatibility validation" },
    password: room.password, session: room.room, ...(role === "host" ? { owner: room.hostToken } : {}) });
  const welcome = await peer.next("welcome");
  if (role === "host") hostWelcomed = true;
  check(`FL ${fl} receives the correct welcome`, welcome.session === room.room && welcome.you?.id === id &&
    welcome.role === (role === "host" ? "leader" : "member") && welcome.transport === 2 && welcome.ends_in === 0 &&
    welcome.compatibility?.protocol === 1 && welcome.compatibility.host_fl === versions[0] &&
    welcome.compatibility.wire === "flim-25-26-v1" && welcome.compatibility.mixed === (role !== "host"));
  const snapshot = await peer.next("snapshot");
  check(`FL ${fl} joins an empty test project`, snapshot.flp === null);
  await peer.next("lock_state");
  return { ...peer, id, fl };
}

async function roundTrip(host, members, sender, label) {
  const marker = randomUUID();
  sender.send({ t: "op", op: { kind: "notes.set", marker } });
  const recipients = [host, ...members].filter(peer => peer !== sender);
  const received = await Promise.all(recipients.map(peer => peer.next("op", message => message.op?.marker === marker)));
  check(label, received.every(message => message.op.kind === "notes.set" && message.from === sender.id &&
    message.source_fl === sender.fl && Number.isSafeInteger(message.seq) && message.seq > 0) &&
    received.every(message => message.seq === received[0].seq));
  return received[0].seq;
}

async function verifyRoomGone() {
  const deadline = Date.now() + 8_000;
  for (;;) {
    const status = await api("/rooms/status", { room: ownedRoom.room, password: ownedRoom.password }, Math.max(1, Math.min(3_000, deadline - Date.now())));
    if (status.status === 404 && status.body.code === "room_unavailable") {
      const join = await api("/rooms/join", { room: ownedRoom.room }, 3_000);
      check("Owned room is unavailable after cleanup", join.status === 404 && join.body.code === "room_unavailable");
      return;
    }
    requireCondition(Date.now() < deadline, "Owned room remained available after socket cleanup");
    await delay(100);
  }
}

async function cleanupReservation() {
  if (hostWelcomed) return;
  const state = await api("/rooms/status", { room: ownedRoom.room, password: ownedRoom.password }, 3_000);
  if (state.status === 404 || (state.status === 200 && state.body.state === "active")) return;
  requireCondition(state.status === 200 && state.body.state === "waiting", "Owned reservation cleanup status was unavailable");
  const cancelled = await request("/rooms/cancel", { room: ownedRoom.room, hostToken: ownedRoom.hostToken }, 3_000);
  if (cancelled.status === 200) return;
  requireCondition(cancelled.status === 404, "Owned reservation cleanup was rejected");
  // Older deployed relays lack cancellation. Activate only our existing reservation, then close it.
  const peer = await open(ownedRoom.websocket);
  try {
    peer.send({ t: "host", transport: 2, compat: compat(versions[1]), user: { id: randomUUID(), name: "Test reservation cleanup" },
      password: ownedRoom.password, session: ownedRoom.room, owner: ownedRoom.hostToken });
    requireCondition((await peer.next("welcome")).session === ownedRoom.room, "Owned reservation could not be released and will expire automatically");
  } finally { await peer.close(); }
}

let failure;
try {
  if (process.env.FLIM_COMPAT_SMOKE_ARTIFACT) {
    requireCondition(!process.env.FLIM_TEST_RELAY, "Choose either a local worker artifact or a relay URL");
    const scriptPath = resolve(process.env.FLIM_COMPAT_SMOKE_ARTIFACT);
    artifactSha256 = createHash("sha256").update(await readFile(scriptPath)).digest("hex");
    const { Miniflare } = await import("miniflare");
    runtime = new Miniflare({ modules: true, scriptPath, modulesRoot: dirname(scriptPath), host: "127.0.0.1", port: 0, compatibilityDate: "2026-07-30",
      durableObjects: { ROOM: { className: "FlimRoom", useSQLite: true } }, r2Buckets: ["FILES"],
      bindings: { HOST_KEY: "isolated-compatibility-test-key-never-deploy" } });
    base = new URL(await runtime.ready);
  } else base = new URL(process.env.FLIM_TEST_RELAY ?? "https://flim-relay.cashflowbeatz.workers.dev");
  requireCondition((base.protocol === "https:" || (base.protocol === "http:" && ["127.0.0.1", "localhost", "[::1]"].includes(base.hostname))) &&
    !base.username && !base.password && !base.search && !base.hash && base.pathname === "/", "Use an HTTPS relay origin or loopback HTTP origin without credentials");
  phase = "health and non-admitting probes";
  const health = await request("/health");
  check("Relay health", health.status === 200 && health.text.trim() === "flim-server ok");
  check("Room API is available", (await request("/rooms")).status === 405);
  for (const fl of versions) await probe(fl, 2, "bad_request", `FL ${fl} passes compatibility before invalid-user rejection`);
  for (const fl of ["24.2.2.4596", "24.2.2.4598"]) await probe(fl, 2, "compatibility", `Unmapped FL ${fl} is refused before admission`);
  await probe("27.0.0.1", 2, "compatibility", "Unsupported FL family is refused before admission");
  await probe(versions[1], 1, "compatibility", "Unsupported transport is refused before admission");
  if (createRoom) {
    phase = "one owned room reservation";
    const created = await api("/rooms", {});
    requireCondition(created.status === 201, "Could not reserve the single test room");
    ownedRoom = created.body; roomReservationsUsed = 1; validateRoom(ownedRoom, true);
    phase = "FL24 host and FL25/26 members";
    const host = await connect(ownedRoom, "host", versions[0]), members = [];
    for (const fl of versions.slice(1)) {
      const joined = await api("/rooms/join", { room: ownedRoom.room });
      check(`FL ${fl} receives member-only credentials`, joined.status === 200 && !joined.body.hostToken && joined.body.room === ownedRoom.room);
      members.push(await connect(joined.body, "join", fl));
    }
    const presence = await host.next("presence", message => message.peers?.length === 3);
    check("FL24 host observes the mixed-build membership", presence.compatibility?.mixed === true && presence.compatibility.host_fl === versions[0]);
    const state = await api("/rooms/status", { room: ownedRoom.room, password: ownedRoom.password });
    check("Room status lists only the three owned peers", state.status === 200 && state.body.state === "active" && state.body.peers?.length === 3 &&
      [host, ...members].every(peer => state.body.peers.some(item => item.id === peer.id && item.fl === peer.fl && item.role === (peer === host ? "leader" : "member"))));
    phase = "small inline note-message round trips";
    const seq25 = await roundTrip(host, members, members[0], "FL25 note marker reaches FL24 host and FL26 member");
    const seq26 = await roundTrip(host, members, members[1], "FL26 note marker reaches FL24 host and FL25 member");
    const seq24 = await roundTrip(host, members, host, "FL24 host reply reaches both members");
    check("Relay sequence increases across all senders", seq25 < seq26 && seq26 < seq24);
  }
} catch (error) { failure = error instanceof SmokeFailure ? error.message : `Unexpected failure during ${phase}`; }
finally {
  phase = "owned socket and room cleanup";
  try {
    await Promise.all(sockets.map(peer => peer.close()));
    if (ownedRoom) {
      await cleanupReservation();
      await verifyRoomGone();
    }
  } catch (error) { failure = [failure, error instanceof SmokeFailure ? error.message : "Owned room cleanup could not be verified"].filter(Boolean).join("; "); }
  if (runtime) {
    try { await runtime.dispose(); }
    catch { failure = [failure, "Local test runtime cleanup failed"].filter(Boolean).join("; "); }
  }
}
console.log(JSON.stringify({ mode: createRoom ? "scoped-room-compatibility" : "read-only-invalid-user-probes",
  target: runtime ? "local-artifact" : "selected-relay", ...(artifactSha256 ? { artifactSha256 } : {}),
  status: failure ? "failed" : "passed", passed: checks.length, checks, roomReservationsUsed,
  r2Uploads: 0, r2Downloads: 0, flProcessesStarted: 0, ...(failure ? { failure } : {}) }));
if (failure) process.exitCode = 1;
