import { DurableObject } from "cloudflare:workers";
import { COMPATIBILITY_PROTOCOL, SHARED_WIRE, validCompatibility, compareFlVersions, type Compatibility } from "../../src/compatibility";
import { validOp, type Op } from "../../src/protocol";
import { admission, operation, stored, hash, key, INLINE_MAX, MESSAGE_MAX, FILE_MAX, FILE_COUNT_MAX, type Stored } from "./policy";
import { r2Limits, r2Period, type R2LimitsEnv } from "./r2-budget";
import { boundedText, BodyReadError } from "./request-body";

interface Env extends R2LimitsEnv { ROOM: DurableObjectNamespace<FlimRoom>; FILES: R2Bucket; HOST_KEY: string }
interface Lock { key: string; at: number }
interface Peer { id: string; name: string; role: "leader" | "member"; compat: Compatibility }
interface Attachment { ip: string; deadline: number; tokens: number; tokenAt: number; peer?: Peer; locks: Lock[]; generation?: string; room?: string; files?: boolean; filesId?: string; fileToken?: string }
interface Room { code: string; accessPassword?: string; generation: string; password: string; blob: string; host: Peer; epoch: number; snapshot?: Stored; notesCrdt?: boolean; portableSync?: boolean }
type FileRow = { h: string; bytes: number; prefix: string; ready: number; created: number };
const encoder = new TextEncoder();
const LOCK_TTL = 30_000;
const MEMBERS = 8;
const ROOMS = 4;
const RESERVATION_TTL = 10 * 60_000;
const QUEUED_BYTES_MAX = 8 * 1024 * 1024;
const QUEUED_MESSAGES_MAX = 8000;
const PEER_QUEUED_BYTES_MAX = 2 * 1024 * 1024;
const PEER_QUEUED_MESSAGES_MAX = 2000;
const roomCode = (value: unknown): value is string => typeof value === "string" && /^\d{8}$/.test(value);
type Reservation = { code: string; password: string; token: string; expires: number };
const json = (value: unknown, status = 200) => new Response(JSON.stringify(value), { status, headers: { "content-type": "application/json", "cache-control": "private, no-store" } });
const hexBytes = (hex: string) => Uint8Array.from(hex.match(/../g)!, h => parseInt(h, 16));
async function digest(text: string): Promise<string> { return Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", encoder.encode(text))), b => b.toString(16).padStart(2, "0")).join(""); }
function random(): string { return Array.from(crypto.getRandomValues(new Uint8Array(32)), b => b.toString(16).padStart(2, "0")).join(""); }
function equal(a: string, b: string): boolean { if (a.length !== b.length) return false; let diff = 0; for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i); return diff === 0; }

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/health") return new Response(env.HOST_KEY?.length >= 24 ? "flim-server ok\n" : "Host key is not configured\n", { status: env.HOST_KEY?.length >= 24 ? 200 : 503 });
    if (!env.HOST_KEY || env.HOST_KEY.length < 24) return json({ error: "Configure a private HOST_KEY before using the relay." }, 503);
    if (request.headers.get("Upgrade")?.toLowerCase() === "websocket" && (url.pathname === "/" || /^\/(?:room|peer)\/(?:\d{8}|01)$/.test(url.pathname))) {
      return env.ROOM.get(env.ROOM.idFromName("private-room-01")).fetch(request);
    }
    if (url.pathname.startsWith("/blob/") || url.pathname === "/blobs/missing" || url.pathname === "/usage" || ["/rooms", "/rooms/join", "/rooms/status", "/rooms/cancel"].includes(url.pathname)) return env.ROOM.get(env.ROOM.idFromName("private-room-01")).fetch(request);
    if (request.method === "GET" && ["/", "/01"].includes(url.pathname)) return new Response("flim Cloudflare relay\nOpen FL Studio through the Cloudflare-enabled flim client.\nHost: Ctrl+click Host, enter this hostname and your private hosting key.\nJoin: paste the /01 session link and enter the session password.\n", { headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" } });
    return new Response("Not found", { status: 404 });
  }
} satisfies ExportedHandler<Env>;

export class FlimRoom extends DurableObject<Env> {
  private rooms = new Map<string, Room>();
  private seq = 0;
  private seqEnd = 0;
  private tail: Promise<unknown> = Promise.resolve();
  private nextAlarm: number | null = null;
  private cleanupPending = false;
  private uploads = new Map<string, AbortController>();
  private queuedBytes = 0;
  private queuedCount = 0;
  private queuedMessages = new Map<WebSocket, { count: number; bytes: number }>();

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    ctx.blockConcurrencyWhile(async () => {
      ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS files (h TEXT NOT NULL, bytes INTEGER NOT NULL, prefix TEXT NOT NULL, ready INTEGER NOT NULL, created INTEGER NOT NULL, PRIMARY KEY(prefix,h))");
      ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS budget (day TEXT NOT NULL, kind TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY(day,kind))");
      ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS r2_usage (month TEXT NOT NULL, kind TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY(month,kind))");
      ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS failures (ip TEXT PRIMARY KEY, n INTEGER NOT NULL, until_ms INTEGER NOT NULL)");
      ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS reservations (code TEXT PRIMARY KEY, password TEXT NOT NULL, token TEXT NOT NULL, expires INTEGER NOT NULL)");
      this.rooms = new Map((await ctx.storage.get<Array<[string, Room]>>("rooms")) ?? []);
      const legacy = await ctx.storage.get<Room>("room");
      if (legacy && !this.rooms.has("01")) { legacy.code = "01"; this.rooms.set("01", legacy); await this.saveRooms(); }
      if (legacy) await ctx.storage.delete("room");
      this.nextAlarm = await ctx.storage.getAlarm();
      this.refreshCleanup();
      for (const room of [...this.rooms.values()]) if (!this.joined(room).some(ws => this.attachment(ws).peer?.role === "leader")) await this.end(room);
      this.seq = (await ctx.storage.get<number>("sequence")) ?? 0;
      this.seqEnd = this.seq + 1_000_000;
      await ctx.storage.put("sequence", this.seqEnd);
      await this.schedule();
    });
  }

  private serial(task: () => Promise<unknown>): Promise<unknown> {
    const next = this.tail.then(task);
    this.tail = next.catch(() => {});
    return next;
  }
  private attachment(ws: WebSocket): Attachment { return ws.deserializeAttachment() as Attachment; }
  private sockets(): WebSocket[] { return this.ctx.getWebSockets().filter(ws => ws.readyState === 1); }
  private joined(room: Room): WebSocket[] { return this.sockets().filter(ws => { const a = this.attachment(ws); return a.peer && a.generation === room.generation; }); }
  private fileSockets(room: Room): WebSocket[] { return this.sockets().filter(ws => { const a = this.attachment(ws); return a.filesId && a.generation === room.generation; }); }
  private filePresence(room: Room): void {
    const peers = this.fileSockets(room).map(ws => this.attachment(ws).filesId!);
    for (const ws of this.fileSockets(room)) this.send(ws, {t: "files.peers", you: this.attachment(ws).filesId, peers});
  }
  private async fileMessage(ws: WebSocket, a: Attachment, m: Record<string, any>): Promise<void> {
    const room = this.rooms.get(a.room ?? "");
    if (!room) return this.fail(ws, "no_session", "This session has ended.", true);
    if (!a.filesId) {
      const ip = await digest(a.ip);
      if (!this.takeBudget("files:admissions", 4000) || !this.takeBudget(`files:admissions:${ip}`, 200)) return this.fail(ws, "rate_limit", "Too many file connections.", true);
      if (Date.now() > a.deadline || m.t !== "files.hello" || !hash(m.token)) return this.fail(ws, "unauthorized", "File connection not authorized.", true);
      const native = this.joined(room).find(s => equal(this.attachment(s).fileToken ?? "", m.token));
      if (!native) return this.fail(ws, "unauthorized", "File connection not authorized.", true);
      a.filesId = this.attachment(native).peer!.id; a.generation = room.generation;
      for (const old of this.fileSockets(room)) if (old !== ws && this.attachment(old).filesId === a.filesId) old.close(4000, "replaced");
      ws.serializeAttachment(a); this.filePresence(room); return;
    }
    if (a.generation !== room.generation || !this.joined(room).some(s => this.attachment(s).peer!.id === a.filesId)) return this.fail(ws, "no_session", "This session has ended.", true);
    if (m.t !== "files.signal" || typeof m.to !== "string" || m.to === a.filesId || !m.signal || typeof m.signal !== "object" || Array.isArray(m.signal)) return this.fail(ws, "bad_signal", "Invalid file signal.");
    const s = m.signal;
    const valid = Object.keys(s).length === 2 && ["offer", "answer"].includes(s.type) && typeof s.sdp === "string" && s.sdp.length <= 32768 ||
      Object.keys(s).length === 2 && s.type === "ice" && s.candidate && typeof s.candidate === "object" && !Array.isArray(s.candidate) &&
      Object.keys(s.candidate).every(k => ["candidate", "sdpMid", "sdpMLineIndex", "usernameFragment"].includes(k)) &&
      typeof s.candidate.candidate === "string" && s.candidate.candidate.length <= 2048 &&
      (s.candidate.sdpMid === null || typeof s.candidate.sdpMid === "string" && s.candidate.sdpMid.length <= 128) &&
      (s.candidate.sdpMLineIndex === null || Number.isInteger(s.candidate.sdpMLineIndex) && s.candidate.sdpMLineIndex >= 0 && s.candidate.sdpMLineIndex < 64) &&
      (s.candidate.usernameFragment === undefined || typeof s.candidate.usernameFragment === "string" && s.candidate.usernameFragment.length <= 128);
    if (!valid) return this.fail(ws, "bad_signal", "Invalid file signal.");
    const target = this.fileSockets(room).find(s => this.attachment(s).filesId === m.to);
    if (target) this.send(target, {t: "files.signal", from: a.filesId, signal: s});
  }
  private active(room: Room): boolean { return this.rooms.get(room.code)?.generation === room.generation; }
  private async saveRooms(): Promise<void> { await this.ctx.storage.put("rooms", [...this.rooms.entries()]); }
  private activePrefixes(): string[] { return this.rooms.size ? [...this.rooms.values()].map(room => room.generation) : [""]; }
  private refreshCleanup(): void {
    const prefixes = this.activePrefixes();
    this.cleanupPending = !!this.ctx.storage.sql.exec(`SELECT h FROM files WHERE prefix NOT IN (${prefixes.map(() => "?").join(",")}) OR ready=0 LIMIT 1`, ...prefixes).toArray().length;
  }
  private send(ws: WebSocket, value: unknown): void { try { ws.send(JSON.stringify(value)); } catch { try { ws.close(1011, "Connection could not keep up"); } catch {} } }
  private broadcast(room: Room, value: any, except?: WebSocket): void {
    const data = JSON.stringify(value);
    for (const ws of this.joined(room)) {
      if (ws === except) continue;
      const peer = this.attachment(ws).peer!;
      const target = value.op?.for ?? value.for;
      if (target !== undefined && target !== peer.id) continue;
      const kind = value.op?.kind ?? value.kind;
      if (room.portableSync && compareFlVersions(peer.compat.fl, value.source_fl ?? room.host.compat.fl) < 0 &&
          (["project.load", "plug.full", "clips.set"].includes(kind) || ["snapshot", "project_swap"].includes(value.t) || ["snapshot", "project_swap"].includes(value.type))) continue;
      try { ws.send(data); } catch { try { ws.close(1011, "Connection could not keep up"); } catch {} }
    }
  }
  private fail(ws: WebSocket, code: string, msg: string, close = false): void { this.send(ws, { t: "error", code, msg }); if (close) { try { ws.close(4001, code); } catch {} } }
  private peers(room: Room) { return this.joined(room).map(ws => { const p = this.attachment(ws).peer!; return { id: p.id, name: p.name, role: p.role, fl: p.compat.fl }; }); }
  private compatibility(room: Room) { const host = room.host.compat; return { protocol: 1, host_fl: host.fl, wire: host.wire, mixed: this.peers(room).some(p => p.fl !== host.fl) }; }
  private noteProtocol(room: Room): { notes_crdt?: 1; portable_sync?: 1 } { return {...(room.notesCrdt ? {notes_crdt: 1 as const} : {}), ...(room.portableSync ? {portable_sync: 1 as const} : {})}; }
  private takeBudget(kind: string, limit: number, window: "day" | "minute" = "day"): boolean {
    const day = new Date().toISOString().slice(0, window === "minute" ? 16 : 10);
    const row = this.ctx.storage.sql.exec<{n: number}>("SELECT n FROM budget WHERE day=? AND kind=?", day, kind).toArray()[0];
    if ((row?.n ?? 0) >= limit) return false;
    this.ctx.storage.sql.exec("INSERT INTO budget(day,kind,n) VALUES(?,?,1) ON CONFLICT(day,kind) DO UPDATE SET n=n+1", day, kind);
    return true;
  }
  private capacity(): boolean {
    this.ctx.storage.sql.exec("DELETE FROM reservations WHERE expires<=?", Date.now());
    return this.rooms.size + this.ctx.storage.sql.exec<{ n: number }>("SELECT count(*) AS n FROM reservations").toArray()[0].n < ROOMS;
  }
  private async roomApi(request: Request, url: URL, text: string): Promise<Response> {
    if (request.method !== "POST") return json({ error: "Method not allowed" }, 405);
    const action = url.pathname === "/rooms" ? "create" : url.pathname.slice(7);
    const ip = await digest(request.headers.get("CF-Connecting-IP") ?? "local");
    const globalLimit = action === "create" ? 60 : action === "cancel" ? 500 : action === "join" ? 2000 : 50_000;
    const ipLimit = action === "create" ? 10 : action === "join" || action === "cancel" ? 100 : 10_000;
    const window = action === "create" ? "minute" : "day";
    if (!this.takeBudget(`api:${action}`, globalLimit, window) || !this.takeBudget(`api:${action}:${ip}`, ipLimit, window)) return json({ error: action === "create" ? "Too many room requests. Try again in a minute." : "Too many room requests. Try again later.", code: "room_rate_limit" }, 429);
    if (this.nextAlarm === null) { const next = Date.now() + 86_400_000; await this.ctx.storage.setAlarm(next); this.nextAlarm = next; }
    let body: Record<string, unknown>;
    try { body = JSON.parse(text || "{}"); } catch { return json({ error: "Expected a JSON object." }, 400); }
    if (!body || typeof body !== "object" || Array.isArray(body)) return json({ error: "Expected a JSON object." }, 400);
    this.ctx.storage.sql.exec("DELETE FROM reservations WHERE expires<=?", Date.now());
    if (action === "create") {
      if (!this.capacity()) return json({ error: "All four rooms are in use. Try again when a host leaves.", code: "relay_full" }, 409);
      let code: string;
      do { code = String(crypto.getRandomValues(new Uint32Array(1))[0] % 90_000_000 + 10_000_000); }
      while (this.rooms.has(code) || this.ctx.storage.sql.exec("SELECT code FROM reservations WHERE code=?", code).toArray().length);
      const password = random(), hostToken = random(), expires = Date.now() + RESERVATION_TTL;
      this.ctx.storage.sql.exec("INSERT INTO reservations(code,password,token,expires) VALUES(?,?,?,?)", code, password, await digest(hostToken), expires);
      await this.schedule();
      return json({ room: code, password, hostToken, expiresAt: new Date(expires).toISOString(), websocket: `/room/${code}`, maxParticipants: MEMBERS }, 201);
    }
    if (!roomCode(body.room)) return json({ error: "Enter the eight-digit room code.", code: "invalid_room" }, 400);
    const room = this.rooms.get(body.room);
    const pending = this.ctx.storage.sql.exec<Reservation>("SELECT * FROM reservations WHERE code=?", body.room).toArray()[0];
    if (action === "cancel") {
      // The host handshake uses the same serial queue, so cancellation cannot end an active room.
      if (room || !pending || typeof body.hostToken !== "string" || !equal(await digest(body.hostToken), pending.token))
        return json({ error: "That reservation is not available.", code: "room_unavailable" }, 404);
      this.ctx.storage.sql.exec("DELETE FROM reservations WHERE code=?", body.room);
      return json({ released: true });
    }
    if (action === "status") {
      const expected = room?.accessPassword ?? pending?.password;
      if (!expected || typeof body.password !== "string" || !equal(body.password, expected)) return json({ error: "That room is not available.", code: "room_unavailable" }, 404);
      return json({ room: body.room, state: room ? "active" : "waiting", peers: room ? this.peers(room) : [], maxParticipants: MEMBERS, ...(room?.portableSync ? {portable_sync: 1} : {}), ...(pending ? { expiresAt: new Date(pending.expires).toISOString() } : {}) });
    }
    if (pending) return json({ error: "The host is opening FL Studio. Try joining again in a moment.", code: "room_not_ready" }, 409);
    if (!room?.accessPassword || !this.joined(room).some(ws => this.attachment(ws).peer?.role === "leader")) return json({ error: "That room is not available. Check the code with your host.", code: "room_unavailable" }, 404);
    if (this.joined(room).length >= MEMBERS) return json({ error: "This room already has eight people.", code: "room_full" }, 409);
    return json({ room: room.code, password: room.accessPassword, websocket: `/room/${room.code}`, maxParticipants: MEMBERS });
  }
  private r2Used(kind: "uploads" | "downloads"): number {
    return this.ctx.storage.sql.exec<{ n: number }>("SELECT n FROM r2_usage WHERE month=? AND kind=?", r2Period().month, kind).toArray()[0]?.n ?? 0;
  }
  private reserveR2(kind: "uploads" | "downloads"): boolean {
    if (this.r2Used(kind) >= r2Limits(this.env)[kind]) return false;
    // No await separates this reservation from its limit check. Failed attempts stay charged.
    this.ctx.storage.sql.exec("INSERT INTO r2_usage(month,kind,n) VALUES(?,?,1) ON CONFLICT(month,kind) DO UPDATE SET n=n+1", r2Period().month, kind);
    return true;
  }
  private storageUsage() {
    return this.ctx.storage.sql.exec<{ bytes: number; n: number }>("SELECT coalesce(sum(bytes),0) AS bytes,count(*) AS n FROM files").toArray()[0];
  }
  private r2LimitResponse(kind: "uploads" | "downloads"): Response {
    const { resetsAt } = r2Period();
    return json({ error: `Monthly R2 ${kind} cutoff reached. File transfers of this type are paused until ${resetsAt}.`, code: "r2_monthly_limit", resets_at: resetsAt }, 429);
  }
  private async usage(request: Request): Promise<Response> {
    if (request.method !== "GET") return json({ error: "Method not allowed" }, 405);
    if (!equal(await digest(request.headers.get("x-flim-owner") ?? ""), await digest(this.env.HOST_KEY))) return json({ error: "Hosting key required" }, 403);
    const limits = r2Limits(this.env), total = this.storageUsage(), period = r2Period();
    const uploads = this.r2Used("uploads"), downloads = this.r2Used("downloads");
    return json({ scope: "This relay only; R2 attempts counted since this guard was installed", month: period.month, resets_at: period.resetsAt,
      storage: { bytes: total.bytes, limit_bytes: limits.bytes, files: total.n, limit_files: FILE_COUNT_MAX },
      uploads: { used: uploads, limit: limits.uploads, blocked: uploads >= limits.uploads || total.bytes >= limits.bytes || total.n >= FILE_COUNT_MAX },
      downloads: { used: downloads, limit: limits.downloads, blocked: downloads >= limits.downloads } });
  }
  private async schedule(): Promise<void> {
    const times: number[] = [];
    for (const ws of this.sockets()) {
      const a = this.attachment(ws);
      if (!a.peer && !a.filesId) times.push(a.deadline);
      for (const l of a.locks) times.push(l.at + LOCK_TTL);
    }
    if (this.cleanupPending) times.push(Date.now() + 60_000);
    const reservation = this.ctx.storage.sql.exec<{ expires: number }>("SELECT min(expires) AS expires FROM reservations").toArray()[0];
    if (reservation?.expires) times.push(reservation.expires);
    if (times.length) {
      const next = Math.max(Date.now() + 100, Math.min(...times));
      if (this.nextAlarm === null || this.nextAlarm > next) { await this.ctx.storage.setAlarm(next); this.nextAlarm = next; }
    }
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    if (url.pathname === "/usage") return this.usage(request);
    if (["/rooms", "/rooms/join", "/rooms/status", "/rooms/cancel"].includes(url.pathname)) {
      if (request.method !== "POST") return json({ error: "Method not allowed" }, 405);
      if (request.headers.get("content-type")?.split(";")[0].trim().toLowerCase() !== "application/json") return json({ error: "Expected application/json." }, 415);
      let text: string;
      try { text = await boundedText(request, 1024); }
      catch (error) { if (error instanceof BodyReadError) return json({ error: error.message }, error.status); throw error; }
      return await this.serial(() => this.roomApi(request, url, text)) as Response;
    }
    if (request.headers.get("Upgrade")?.toLowerCase() === "websocket") {
      if (this.sockets().length >= 80) return new Response("Too many connections", { status: 429 });
      const pair = new WebSocketPair();
      const [client, server] = Object.values(pair);
      this.ctx.acceptWebSocket(server);
      server.serializeAttachment({ ip: request.headers.get("CF-Connecting-IP") ?? "local", deadline: Date.now() + 20_000, tokens: 2000, tokenAt: Date.now(), locks: [], room: url.pathname === "/" ? "01" : url.pathname.slice(6), files: url.pathname.startsWith("/peer/") } satisfies Attachment);
      await this.schedule();
      return new Response(null, { status: 101, webSocket: client });
    }
    return this.files(request, url);
  }

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    if (ws.readyState !== 1) return;
    const bytes = typeof message === "string" && message.length <= INLINE_MAX ? encoder.encode(message).length : INLINE_MAX + 1;
    if (bytes > INLINE_MAX) {
      this.fail(ws, "bad_request", "Large messages must use flim's R2 file transport.", true);
      await this.serial(() => this.remove(ws)); return;
    }
    const pending = this.queuedMessages.get(ws) ?? { count: 0, bytes: 0 };
    if (pending.count >= PEER_QUEUED_MESSAGES_MAX || pending.bytes + bytes > PEER_QUEUED_BYTES_MAX || this.queuedBytes + bytes > QUEUED_BYTES_MAX || this.queuedCount >= QUEUED_MESSAGES_MAX) {
      this.fail(ws, "relay_busy", "The relay cannot keep up with this connection. Reconnect and resync before editing.", true);
      await this.serial(() => this.remove(ws)); return;
    }
    pending.count++; pending.bytes += bytes; this.queuedBytes += bytes; this.queuedCount++;
    this.queuedMessages.set(ws, pending);
    try {
      await this.serial(async () => {
        if (ws.readyState !== 1) return;
        try { await this.message(ws, message as string); }
        catch { this.fail(ws, "server_error", "The relay could not process this request. Reconnect and resync before editing.", true); }
        if (ws.readyState !== 1) await this.remove(ws);
      });
    } finally {
      pending.count--; pending.bytes -= bytes; this.queuedBytes -= bytes; this.queuedCount--;
      if (!pending.count) this.queuedMessages.delete(ws);
    }
  }
  private async message(ws: WebSocket, data: string): Promise<void> {
    const a = this.attachment(ws), now = Date.now();
    a.tokens = Math.min(2000, a.tokens + (now - a.tokenAt) * 0.2); a.tokenAt = now;
    if (a.tokens < 1) return this.fail(ws, "rate_limit", "Too many messages; reconnect after reducing edit traffic.", true);
    a.tokens--; ws.serializeAttachment(a);
    let m: Record<string, any>;
    try { m = JSON.parse(data); } catch { return this.fail(ws, "bad_json", "Malformed JSON.", true); }
    if (!m || typeof m !== "object" || Array.isArray(m)) return this.fail(ws, "bad_request", "Expected an object.");
    if (a.files) return this.fileMessage(ws, a, m);
    if (!a.peer) return this.hello(ws, a, m);
    const room = this.rooms.get(a.room ?? "01");
    if (!room || a.generation !== room.generation) return this.fail(ws, "no_session", "This session has ended.", true);
    this.sweepLocks(room);
    switch (m.t) {
      case "op":
        if (!validOp(m.op)) return this.fail(ws, "bad_request", "Malformed operation.");
        return this.relay(room, ws, a, m.op);
      case "stored": {
        if (!stored(m)) return this.fail(ws, "bad_request", "Malformed file reference.");
        const row = this.fileRow(room, m.h);
        if (!row?.ready || row.bytes !== m.s) return this.fail(ws, "bad_request", "Upload the complete file before referencing it.");
        if (m.type === "op") return this.relay(room, ws, a, { kind: m.kind!, ...(m.key ? { key: m.key } : {}), ...(m.for ? {for: m.for} : {}) }, m);
        return this.snapshot(room, ws, a, m);
      }
      case "snapshot": case "project_swap": {
        if (a.peer.role !== "leader") return this.fail(ws, "not_leader", "Only the host can replace the project.");
        if (typeof m.flp !== "string") return this.fail(ws, "bad_request", "Malformed project snapshot.");
        const text = JSON.stringify({ t: m.t, flp: m.flp }), h = await digest(text);
        const ref: Stored = { t: "stored", type: m.t, h, s: encoder.encode(text).length };
        const response = await this.files(new Request("https://flim/blob/" + h, { method: "PUT", headers: { "x-flim-key": room.blob, "content-length": String(ref.s) }, body: text }), new URL("https://flim/blob/" + h));
        if (!response.ok) { const problem = await response.json() as { error?: string }; return this.fail(ws, "storage_limit", problem.error ?? "The project snapshot could not be stored."); }
        return this.snapshot(room, ws, a, ref);
      }
      case "lock_acquire": {
        if (!key(m.key)) return this.fail(ws, "bad_request", "Malformed lock key.");
        const owner = this.lockOwner(room, m.key);
        if (owner && owner !== a.peer.id) return this.send(ws, { t: "lock", key: m.key, granted: false, owner });
        const live = this.attachment(ws); live.locks = live.locks.filter(l => l.key !== m.key);
        if (live.locks.length >= 32) return this.fail(ws, "lock_limit", "Release an edit lock before acquiring more.");
        live.locks.push({ key: m.key, at: now });
        if (encoder.encode(JSON.stringify(live)).length > 1900) return this.fail(ws, "lock_limit", "Release an edit lock before acquiring more.");
        ws.serializeAttachment(live);
        this.broadcast(room, { t: "lock", key: m.key, granted: true, owner: a.peer.id });
        await this.schedule(); return;
      }
      case "lock_release": {
        if (!key(m.key)) return this.fail(ws, "bad_request", "Malformed lock key.");
        const live = this.attachment(ws);
        if (live.locks.some(l => l.key === m.key)) { live.locks = live.locks.filter(l => l.key !== m.key); ws.serializeAttachment(live); this.broadcast(room, { t: "lock_released", key: m.key }); }
        return;
      }
      case "bye": ws.close(1000, "bye"); return;
      default: return this.fail(ws, "bad_request", "Unsupported message.");
    }
  }

  private async hello(ws: WebSocket, a: Attachment, m: Record<string, any>): Promise<void> {
    if (Date.now() > a.deadline || !["host", "join"].includes(m.t)) return this.fail(ws, "not_joined", "Host or join a session first.", true);
    if (m.transport !== 2) return this.fail(ws, "compatibility", "This flim client uses an unsupported file-transfer protocol. Open FL Studio through the current flim room app. Cloudflare runs on the relay; no separate installation is needed.", true);
    if (m.notes_crdt !== undefined && m.notes_crdt !== 1) return this.fail(ws, "compatibility", "This client uses an unsupported note merge protocol. Update flim on every computer.", true);
    if (m.portable_sync !== undefined && m.portable_sync !== 1) return this.fail(ws, "compatibility", "This client uses an unsupported portable sync protocol. Update flim on every computer.", true);
    if (!validCompatibility(m.compat)) {
      const c = m.compat;
      const unsupportedBuild = c && typeof c === "object" && !Array.isArray(c) && Object.keys(c).length === 3 &&
        c.protocol === COMPATIBILITY_PROTOCOL && c.wire === SHARED_WIRE && typeof c.fl === "string" &&
        /^(?:0|[1-9]\d{0,4})(?:\.(?:0|[1-9]\d{0,4})){3}$/.test(c.fl) && c.fl.split(".").every((part: string) => Number(part) <= 65535);
      return this.fail(ws, "compatibility", unsupportedBuild
        ? "The relay does not support this FL Studio build. Choose a supported build or ask the relay owner to update compatibility. Cloudflare runs on the relay; no separate installation is needed."
        : "This flim client sent invalid or incompatible connection details. Use the current flim room app on every computer. Cloudflare runs on the relay; no separate installation is needed.", true);
    }
    if (!m.user || typeof m.user.id !== "string" || !/^[a-zA-Z0-9_-]{1,64}$/.test(m.user.id) || typeof m.user.name !== "string" ||
        !m.user.name.trim() || m.user.name.length > 64 || typeof m.password !== "string" || !m.password || m.password.length > 128) return this.fail(ws, "bad_request", "Invalid user or session password.", true);
    const code = a.room ?? "01";
    if (code !== "01" && !roomCode(code)) return this.fail(ws, "no_session", "That room is not available.", true);
    const ip = await digest(a.ip);
    if (!this.takeBudget("ws:admissions", 4000) || !this.takeBudget(`ws:admissions:${ip}`, 200)) return this.fail(ws, "rate_limit", "The relay connection-attempt allowance has been reached. Try again later.", true);
    const fails = this.ctx.storage.sql.exec<{n: number; until_ms: number}>("SELECT n,until_ms FROM failures WHERE ip=?", ip).toArray()[0];
    if (fails && fails.n >= 8 && fails.until_ms > Date.now()) return this.fail(ws, "too_many_attempts", "Too many incorrect passwords. Try again in ten minutes.", true);
    const password = await digest(m.password);
    const peer: Peer = { id: m.user.id, name: m.user.name.trim().replace(/[\x00-\x1f]/g, ""), role: m.t === "host" ? "leader" : "member", compat: m.compat };
    let room = this.rooms.get(code);
    const reservation = this.ctx.storage.sql.exec<Reservation>("SELECT * FROM reservations WHERE code=? AND expires>?", code, Date.now()).toArray()[0];
    const wrong = m.t === "host"
      ? typeof m.owner !== "string" || (code === "01" ? !equal(await digest(m.owner), await digest(this.env.HOST_KEY)) : !reservation || !equal(await digest(m.owner), reservation.token) || !equal(m.password, reservation.password))
      : !!room && !equal(password, room.password);
    if (wrong) {
      this.ctx.storage.sql.exec("INSERT INTO failures(ip,n,until_ms) VALUES(?,1,?) ON CONFLICT(ip) DO UPDATE SET n=CASE WHEN until_ms<? THEN 1 ELSE n+1 END,until_ms=?", ip, Date.now()+600_000, Date.now(), Date.now()+600_000);
      return this.fail(ws, m.t === "host" ? "bad_owner" : "bad_password", "Incorrect hosting key or session password.", true);
    }
    if (m.t === "host") {
      if (room && this.joined(room).some(s => this.attachment(s).peer?.role === "leader")) return this.fail(ws, "full", "That room is already running.", true);
      if (room) await this.end(room);
      if (code === "01" && !this.capacity()) return this.fail(ws, "rate_limit", "All four rooms are in use. Try again when a host leaves.", true);
      room = { code, ...(reservation ? { accessPassword: reservation.password } : {}), generation: random(), password, blob: random(), host: peer, epoch: 0, notesCrdt: m.notes_crdt === 1, portableSync: m.portable_sync === 1 };
      this.rooms.set(code, room);
      this.ctx.storage.sql.exec("DELETE FROM reservations WHERE code=?", code);
      await this.saveRooms();
    } else {
      if (room && !this.joined(room).some(s => this.attachment(s).peer?.role === "leader")) { await this.end(room); room = undefined; }
      if (!room || m.session !== code) return this.fail(ws, "no_session", "That session is not running.", true);
      if (room.portableSync && m.portable_sync !== 1) return this.fail(ws, "compatibility", "This room supports any host order. Open it with the current flim app on every computer.", true);
      const error = admission(m.compat, room.host.compat, room.portableSync);
      if (error) return this.fail(ws, "compatibility", error, true);
      if (room.notesCrdt && m.notes_crdt !== 1) return this.fail(ws, "compatibility", "This room merges simultaneous piano-roll edits. Open it with the current flim app on every computer.", true);
      if (this.joined(room).length >= MEMBERS) return this.fail(ws, "session_full", "This session already has eight people.", true);
      if (this.joined(room).some(s => this.attachment(s).peer!.id === peer.id)) return this.fail(ws, "id_in_use", "This user is already connected.", true);
    }
    a.peer = peer; a.generation = room!.generation; a.locks = []; a.fileToken = random(); ws.serializeAttachment(a);
    this.send(ws, { t: "welcome", session: code, role: peer.role, you: { id: peer.id, name: peer.name }, peers: this.peers(room!), blob: room!.blob, file_peer: a.fileToken, ends_in: 0, transport: 2, ...this.noteProtocol(room!), compatibility: this.compatibility(room!) });
    if (room!.snapshot && (!room!.portableSync || compareFlVersions(peer.compat.fl, room!.host.compat.fl) >= 0)) this.send(ws, { ...room!.snapshot, delivery: "snapshot", epoch: room!.epoch });
    else this.send(ws, { t: "snapshot", flp: null, epoch: room!.epoch });
    this.send(ws, { t: "lock_state", locks: this.joined(room!).flatMap(s => this.attachment(s).locks.map(l => ({ key: l.key, owner: this.attachment(s).peer!.id }))) });
    this.broadcast(room!, { t: "presence", peers: this.peers(room!), ...this.noteProtocol(room!), compatibility: this.compatibility(room!) }, ws);
    await this.schedule();
  }
  private async relay(room: Room, ws: WebSocket, a: Attachment, op: Op, ref?: Stored): Promise<void> {
    const error = operation(op, a.peer!.role === "leader", a.peer!.compat, room.host.compat, this.compatibility(room).mixed, room.portableSync);
    if (error) return this.fail(ws, "compatibility", error);
    if (!ref && (op.kind === "notes.set" || op.kind === "notes.full")) {
      const items = op.kind === "notes.set" ? [op] : Array.isArray(op.items) ? op.items : [];
      if ((room.notesCrdt && op.kind === "notes.full" && !Array.isArray(op.items)) ||
          items.some(item => room.notesCrdt ? item?.crdt !== 1 : item?.crdt !== undefined))
        return this.fail(ws, "compatibility", "The note edit does not match this room's negotiated merge protocol.");
    }
    const owner = op.key ? this.lockOwner(room, op.key) : undefined;
    if (owner && owner !== a.peer!.id) return this.fail(ws, "locked", "This part of the project is being edited by another person.");
    if (op.kind === "project.load") this.releaseLocks(room);
    else if (op.kind !== "cur") { const live = this.attachment(ws); live.locks.forEach(l => l.at = Date.now()); ws.serializeAttachment(live); }
    if (this.seq >= this.seqEnd) { this.seqEnd += 1_000_000; await this.ctx.storage.put("sequence", this.seqEnd); }
    const metadata = { seq: ++this.seq, from: a.peer!.id, source_fl: a.peer!.compat.fl };
    this.broadcast(room, ref ? { ...ref, ...metadata } : { t: "op", ...metadata, op }, ws);
    await this.schedule();
  }
  private async snapshot(room: Room, ws: WebSocket, a: Attachment, ref: Stored): Promise<void> {
    if (a.peer!.role !== "leader") return this.fail(ws, "not_leader", "Only the host can replace the project.");
    room.snapshot = ref;
    if (ref.type === "project_swap") { room.epoch++; this.releaseLocks(room); }
    await this.saveRooms();
    this.broadcast(room, { ...ref, epoch: room.epoch }, ws);
  }
  private lockOwner(room: Room, k: string): string | undefined { for (const ws of this.joined(room)) if (this.attachment(ws).locks.some(l => l.key === k)) return this.attachment(ws).peer!.id; return undefined; }
  private releaseLocks(room: Room): void { for (const ws of this.joined(room)) { const a = this.attachment(ws); for (const l of a.locks) this.broadcast(room, { t: "lock_released", key: l.key }); a.locks = []; ws.serializeAttachment(a); } }
  private sweepLocks(room: Room): void { for (const ws of this.joined(room)) { const a = this.attachment(ws); const expired = a.locks.filter(l => l.at + LOCK_TTL <= Date.now()); if (expired.length) { a.locks = a.locks.filter(l => l.at + LOCK_TTL > Date.now()); ws.serializeAttachment(a); for (const l of expired) this.broadcast(room, { t: "lock_released", key: l.key }); } } }
  async webSocketClose(ws: WebSocket, code: number, reason: string): Promise<void> {
    try { ws.close(code, reason); } catch { try { ws.close(1000, "Connection closed"); } catch {} }
    await this.serial(() => this.remove(ws));
  }
  async webSocketError(ws: WebSocket): Promise<void> { try { ws.close(1011, "Connection error"); } catch {} await this.serial(() => this.remove(ws)); }
  private async remove(ws: WebSocket): Promise<void> {
    const a = this.attachment(ws);
    const room = this.rooms.get(a?.room ?? "01");
    if (room && a?.filesId && a.generation === room.generation) { a.filesId = undefined; ws.serializeAttachment(a); this.filePresence(room); return; }
    if (!room || !a?.peer || a.generation !== room.generation) return;
    for (const file of this.fileSockets(room)) if (this.attachment(file).filesId === a.peer.id) file.close(4000, "native_left");
    a.locks.forEach(l => this.broadcast(room, { t: "lock_released", key: l.key }));
    if (a.peer.role === "leader") { this.broadcast(room, { t: "leader_left" }, ws); await this.end(room); }
    else { a.peer = undefined; a.locks = []; ws.serializeAttachment(a); this.broadcast(room, { t: "presence", peers: this.peers(room), ...this.noteProtocol(room), compatibility: this.compatibility(room) }); }
    await this.schedule();
  }
  private async end(room: Room): Promise<void> {
    const sockets = [...this.joined(room), ...this.fileSockets(room)]; this.rooms.delete(room.code); await this.saveRooms();
    for (const [objectKey, abort] of this.uploads) if (objectKey.startsWith(room.generation + "/")) abort.abort();
    this.cleanupPending = true;
    for (const ws of sockets) { const a = this.attachment(ws); a.peer = undefined; a.locks = []; ws.serializeAttachment(a); try { ws.close(4000, "leader_left"); } catch {} }
    await this.schedule();
  }

  private fileRow(room: Room, h: string): FileRow | undefined { return this.ctx.storage.sql.exec<FileRow>("SELECT * FROM files WHERE prefix=? AND h=?", room.generation, h).toArray()[0]; }
  private async files(request: Request, url: URL): Promise<Response> {
    const room = [...this.rooms.values()].find(room => equal(request.headers.get("x-flim-key") ?? "", room.blob));
    if (!room || !this.joined(room).some(s => this.attachment(s).peer?.role === "leader")) return json({ error: "Session file access has expired." }, 403);
    const generation = room.generation;
    if (url.pathname === "/blobs/missing" && request.method === "POST") {
      let text: string;
      try { text = await boundedText(request, 40_000); }
      catch (error) { if (error instanceof BodyReadError) return json({ error: error.message }, error.status); throw error; }
      if (!this.active(room)) return json({ error: "Session file access has expired." }, 403);
      const hashes = text.trim() ? text.trim().split(/\s+/) : [];
      if (hashes.length > 512 || !hashes.every(hash)) return json({ error: "Expected up to 512 hashes, one per line" }, 400);
      return new Response([...new Set(hashes)].filter(h => !this.fileRow(room, h)?.ready).join("\n"), { headers: { "content-type": "text/plain", "cache-control": "private, no-store" } });
    }
    const h = url.pathname.slice(6);
    if (!url.pathname.startsWith("/blob/") || !hash(h)) return json({ error: "Not found" }, 404);
    const objectKey = generation + "/" + h;
    if (request.method === "GET" || request.method === "HEAD") {
      const row = this.fileRow(room, h); if (!row?.ready) return json({ error: "Not found" }, 404);
      if (request.method === "HEAD") return new Response(null, { headers: { "content-length": String(row.bytes), "cache-control": "private, no-store" } });
      if (!this.takeBudget("reads", 5000)) return json({ error: "Daily file-download allowance reached" }, 429);
      if (!this.reserveR2("downloads")) return this.r2LimitResponse("downloads");
      const object = await this.env.FILES.get(objectKey);
      if (!this.active(room)) return json({ error: "Session file access has expired." }, 403);
      if (!object) {
        this.ctx.storage.sql.exec("DELETE FROM files WHERE prefix=? AND h=?", generation, h);
        return json({ error: "Not found" }, 404);
      }
      return new Response(object.body, { headers: { "content-type": "application/octet-stream", "content-length": String(object.size), "cache-control": "private, no-store", "etag": '"' + h + '"' } });
    }
    if (request.method !== "PUT") return json({ error: "Method not allowed" }, 405);
    const raw = request.headers.get("content-length");
    const bytes = Number(raw);
    if (!raw || !/^\d+$/.test(raw) || !Number.isSafeInteger(bytes) || bytes < 1 || bytes > FILE_MAX) return json({ error: "Files must be 1–90,000,000 bytes" }, 413);
    const existing = this.fileRow(room, h);
    if (existing) return existing.ready && existing.bytes === bytes ? new Response(null, { status: 200 }) : json({ error: "Upload already in progress or size mismatch" }, 409);
    const total = this.storageUsage(), limits = r2Limits(this.env);
    if (total.bytes + bytes > limits.bytes || total.n >= FILE_COUNT_MAX) return json({ error: "Private relay storage cutoff reached. End the session to allow temporary files to be cleaned up.", code: "r2_storage_limit", limit_bytes: limits.bytes, limit_files: FILE_COUNT_MAX }, 507);
    if (!this.takeBudget("uploads", 1000)) return json({ error: "Daily upload allowance reached" }, 429);
    if (!this.reserveR2("uploads")) return this.r2LimitResponse("uploads");
    this.ctx.storage.sql.exec("INSERT INTO files(h,bytes,prefix,ready,created) VALUES(?,?,?,0,?)", h, bytes, generation, Date.now());
    const transferAbort = new AbortController();
    this.uploads.set(objectKey, transferAbort);
    this.cleanupPending = true;
    const transferTimeout = setTimeout(() => transferAbort.abort(), 600_000);
    let putTask: Promise<R2Object | null> | undefined;
    let transferTask: Promise<void> | undefined;
    try {
      await this.schedule();
      if (!request.body) throw new Error("Missing body");
      if (!this.active(room)) throw new Error("Session changed");
      const stream = new FixedLengthStream(bytes);
      transferTask = request.body.pipeTo(stream.writable, { signal: transferAbort.signal });
      putTask = this.env.FILES.put(objectKey, stream.readable, { sha256: hexBytes(h) });
      const [object] = await Promise.all([putTask, transferTask]);
      if (!object || object.size !== bytes || !this.active(room)) throw new Error("Upload size or session changed");
      const reservation = this.ctx.storage.sql.exec<FileRow>("SELECT * FROM files WHERE prefix=? AND h=?", generation, h).toArray()[0];
      if (!reservation || reservation.ready || reservation.bytes !== bytes) throw new Error("Upload reservation expired");
      this.ctx.storage.sql.exec("UPDATE files SET ready=1 WHERE prefix=? AND h=?", generation, h);
      this.refreshCleanup();
      return new Response(null, { status: 201 });
    } catch {
      transferAbort.abort();
      // A rejected body pipe can precede R2 completion; delete only after the put has stopped.
      await Promise.allSettled([putTask, transferTask]);
      try {
        await this.env.FILES.delete(objectKey);
        this.ctx.storage.sql.exec("DELETE FROM files WHERE prefix=? AND h=?", generation, h);
      } catch { /* Keep the reservation until the cleanup alarm can confirm deletion. */ }
      return json({ error: "Upload failed: checksum, length or session mismatch" }, 400);
    } finally {
      clearTimeout(transferTimeout);
      this.uploads.delete(objectKey);
      this.refreshCleanup();
      await this.schedule();
    }
  }

  async alarm(): Promise<void> {
    await this.serial(async () => {
      this.nextAlarm = null;
      try {
        for (const room of this.rooms.values()) this.sweepLocks(room);
        for (const ws of this.sockets()) { const a = this.attachment(ws); if (!a.peer && !a.filesId && a.deadline <= Date.now()) ws.close(4001, "join_timeout"); }
        const prefixes = this.activePrefixes();
        const old = this.ctx.storage.sql.exec<FileRow>(`SELECT * FROM files WHERE prefix NOT IN (${prefixes.map(() => "?").join(",")}) OR (ready=0 AND created<?) LIMIT ?`, ...prefixes, Date.now()-600_000, FILE_COUNT_MAX).toArray();
        // Pick the deletion batch after skipping live transfers, so they cannot hide abandoned files.
        const abandoned = old.filter(file => !this.uploads.has(file.prefix + "/" + file.h)).slice(0, 64);
        if (abandoned.length) {
          await this.env.FILES.delete(abandoned.map(file => file.prefix + "/" + file.h));
          for (const file of abandoned) this.ctx.storage.sql.exec("DELETE FROM files WHERE prefix=? AND h=?", file.prefix, file.h);
        }
        this.refreshCleanup();
        this.ctx.storage.sql.exec("DELETE FROM reservations WHERE expires<=?", Date.now());
        this.ctx.storage.sql.exec("DELETE FROM failures WHERE until_ms<?", Date.now());
        this.ctx.storage.sql.exec("DELETE FROM budget WHERE day<?", new Date(Date.now()-2*86400_000).toISOString().slice(0,10));
      } finally { await this.schedule(); }
    });
  }
}
