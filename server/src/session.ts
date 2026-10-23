import crypto from "crypto";
import { WebSocket } from "ws";
import { LOCK_TTL_MS, Op, PeerInfo, Role, ServerMsg, UnitKey, UserInfo } from "./protocol";
import { Compatibility, compareFlVersions, SessionCompatibility, validCompatibility } from "./compatibility";

export interface Client {
  ws: WebSocket;
  user: UserInfo;
  role: Role;
  alive: boolean; // for ping/pong liveness
  ip: string;
  compat?: Compatibility;
}

interface Lock {
  owner: string; // user id
  at: number;
}

export function send(ws: WebSocket, msg: ServerMsg): void {
  if (ws.readyState === WebSocket.OPEN) sendSerialized(ws, JSON.stringify(msg));
}

// Allows a full 64 MiB project message while bounding queues for peers that stop reading.
export const MAX_BUFFERED_BYTES = 128 * 1024 * 1024;
const MAX_LOCKS = 4096;
const LEADER_ONLY_OPS = new Set(["project.load", "plug.full", "notes.full", "state.full"]);
const SHARED_OPS = new Set([
  "project.load", "plug.full", "notes.full", "state.full", "state.req", "plug.state", "chan.add", "chan.del",
  "notes.set", "clips.set", "meta.set", "pat.op", "focus", "cur", "fx.clear", "chan.order", "param.set",
]);
const OPAQUE_OPS = new Set(["plug.state", "chan.add"]);

export function sendSerialized(ws: WebSocket, data: string, bytes = Buffer.byteLength(data)): void {
  if (ws.readyState !== WebSocket.OPEN) return;
  if (ws.bufferedAmount + bytes > MAX_BUFFERED_BYTES) {
    ws.terminate();
    return;
  }
  ws.send(data, (err) => { if (err) ws.terminate(); });
}

const sha = (v: string) => crypto.createHash("sha256").update(v, "utf8").digest();

// One session = one slot. Everything a session holds (clients, locks, snapshot, ops, sample
// files via blobKey) stays inside it: no message from one session ever reaches another.
export class Session {
  readonly code: string;
  readonly blobKey = crypto.randomBytes(32).toString("hex"); // sample store access (blobs.ts)
  leaderId: string;
  lastActive = Date.now(); // any message from any member; idle sessions are closed
  readonly endsAt: number | null; // time limit (null = none)
  private pwHash: Buffer;
  private ended = false;
  private clients = new Set<Client>();
  private builds = new Map<Client, Compatibility | undefined>();
  private readonly hostCompatibility: Compatibility | undefined;
  private locks = new Map<UnitKey, Lock>();
  private seq = 0;
  private flp: string | null = null;
  private epoch = 0;
  private onEmpty: () => void;

  constructor(code: string, password: string, leader: Client, endsAt: number | null, onEmpty: () => void) {
    if (leader.compat !== undefined && !validCompatibility(leader.compat)) throw new Error("Invalid host compatibility descriptor");
    this.hostCompatibility = leader.compat ? { ...leader.compat } : undefined;
    this.code = code;
    this.endsAt = endsAt;
    this.pwHash = sha(password);
    this.leaderId = leader.user.id;
    this.onEmpty = onEmpty;
    this.add(leader);
  }

  passwordOk(given: unknown): boolean {
    return crypto.timingSafeEqual(sha(typeof given === "string" ? given : ""), this.pwHash);
  }

  hasUser(id: string): boolean {
    for (const c of this.clients) if (c.user.id === id) return true;
    return false;
  }

  touch(): void {
    this.lastActive = Date.now();
  }

  ips(): string[] {
    return [...new Set([...this.clients].map((c) => c.ip))];
  }

  // Close the session for everyone (idle / time limit): the slot is free again.
  end(code: string, msg: string, cooldown?: number): void {
    for (const c of this.clients) {
      send(c.ws, { t: "error", code, msg, ...(cooldown ? { cooldown } : {}) });
      c.ws.close(4002, code);
    }
    this.clients.clear();
    this.builds.clear();
    this.locks.clear();
    this.flp = null;
    this.finish();
  }

  private finish(): void {
    if (this.ended) return;
    this.ended = true;
    this.onEmpty();
  }

  get size(): number {
    return this.clients.size;
  }

  private peers(): PeerInfo[] {
    return [...this.clients].map((c) => ({ id: c.user.id, name: c.user.name, role: c.role,
      ...(this.builds.get(c) ? { fl: this.builds.get(c)!.fl } : {}) }));
  }

  private compatibility(): SessionCompatibility | undefined {
    const host = this.hostCompatibility;
    return host ? { protocol: host.protocol, host_fl: host.fl, wire: host.wire,
      mixed: [...this.builds.values()].some((build) => build!.fl !== host.fl) } : undefined;
  }

  admissionError(client: Client): string | null {
    const build = client.compat, host = this.hostCompatibility;
    if (build !== undefined && !validCompatibility(build)) return "Invalid FL compatibility descriptor.";
    if (!!build !== !!host) return "Everyone must use the same compatibility-aware flim client. Update flim and start a new session.";
    if (build && host && compareFlVersions(build.fl, host.fl) < 0) {
      return `The host uses FL Studio ${host.fl}. The person with the oldest FL Studio build must host a mixed-build session.`;
    }
    return null;
  }

  private broadcast(msg: ServerMsg, except?: Client): void {
    const data = JSON.stringify(msg);
    const bytes = Buffer.byteLength(data);
    for (const c of this.clients) if (c !== except) sendSerialized(c.ws, data, bytes);
  }

  private clientOf(ws: WebSocket): Client | undefined {
    for (const c of this.clients) if (c.ws === ws) return c;
    return undefined;
  }

  add(client: Client): boolean {
    if (this.clients.has(client)) {
      send(client.ws, { t: "error", code: "bad_request", msg: "This client is already in the session." });
      return false;
    }
    const error = this.admissionError(client);
    if (error) {
      send(client.ws, { t: "error", code: "compatibility", msg: error });
      return false;
    }
    this.sweepLocks();
    this.clients.add(client);
    this.builds.set(client, client.compat ? { ...client.compat } : undefined);
    const compatibility = this.compatibility();
    send(client.ws, {
      t: "welcome", session: this.code, role: client.role, you: client.user, peers: this.peers(), blob: this.blobKey,
      ends_in: this.endsAt === null ? 0 : Math.max(1, this.endsAt - Date.now()),
      ...(compatibility ? { compatibility } : {}),
    });
    send(client.ws, { t: "snapshot", flp: this.flp, epoch: this.epoch });
    send(client.ws, { t: "lock_state", locks: [...this.locks].map(([key, l]) => ({ key, owner: l.owner })) });
    this.broadcast({ t: "presence", peers: this.peers(), ...(compatibility ? { compatibility } : {}) }, client);
    return true;
  }

  // Leader seeds the initial project blob (host upload).
  setSnapshot(from: Client, flp: string): void {
    if (from.user.id !== this.leaderId) {
      send(from.ws, { t: "error", code: "not_leader", msg: "Only the party leader can set the project." });
      return;
    }
    this.flp = flp;
    // epoch unchanged for the initial seed; members already got it via welcome/snapshot
    this.broadcast({ t: "snapshot", flp: this.flp, epoch: this.epoch }, from);
  }

  // Leader swaps the whole project; members must resync. Clears all locks.
  projectSwap(from: Client, flp: string): void {
    if (from.user.id !== this.leaderId) {
      send(from.ws, { t: "error", code: "not_leader", msg: "Can't change FLP as non party leader" });
      return;
    }
    this.flp = flp;
    this.epoch += 1;
    this.releaseProjectLocks();
    this.broadcast({ t: "project_swap", flp, epoch: this.epoch }, from);
  }

  relayOp(from: Client, op: Op): void {
    if (!this.clients.has(from)) {
      send(from.ws, { t: "error", code: "not_joined", msg: "Join this session before sending edits." });
      return;
    }
    if (LEADER_ONLY_OPS.has(op.kind) && from.user.id !== this.leaderId) {
      send(from.ws, { t: "error", code: "not_leader", msg: "Only the party leader can replace the project or full state." });
      return;
    }
    const compatibility = this.compatibility();
    const source = this.builds.get(from);
    if (compatibility?.mixed && (!SHARED_OPS.has(op.kind) ||
        (OPAQUE_OPS.has(op.kind) && source!.fl !== compatibility.host_fl))) {
      send(from.ws, { t: "error", code: "compatibility",
        msg: OPAQUE_OPS.has(op.kind)
          ? "The host's FL Studio build must create channels and send plugin presets in a mixed-build session."
          : "This operation is not supported in a mixed-build session." });
      return;
    }
    const now = Date.now();
    // Backstop: if the op targets a unit locked by someone else, reject it.
    if (op.key) {
      const l = this.activeLock(op.key, now);
      if (l && l.owner !== from.user.id) {
        send(from.ws, { t: "error", code: "locked", msg: `Unit ${op.key} is locked by ${l.owner}` });
        return;
      }
    }
    if (op.kind === "project.load") this.releaseProjectLocks();
    else if (op.kind !== "cur") this.touchLocksOf(from.user.id, now);
    this.seq += 1;
    this.broadcast({ t: "op", seq: this.seq, from: from.user.id, ...(source ? { source_fl: source.fl } : {}), op }, from);
  }

  acquire(from: Client, key: UnitKey): void {
    const now = Date.now();
    const existing = this.activeLock(key, now);
    if (!existing && this.locks.size >= MAX_LOCKS) this.sweepLocks(now);
    if (!existing && this.locks.size >= MAX_LOCKS) {
      send(from.ws, { t: "error", code: "lock_limit", msg: "Too many active locks. Release a lock or wait for expiry." });
      return;
    }
    if (existing && existing.owner !== from.user.id) {
      send(from.ws, { t: "lock", key, granted: false, owner: existing.owner });
      return;
    }
    this.locks.set(key, { owner: from.user.id, at: now });
    send(from.ws, { t: "lock", key, granted: true, owner: from.user.id });
    this.broadcast({ t: "lock", key, granted: true, owner: from.user.id }, from);
  }

  release(from: Client, key: UnitKey): void {
    const l = this.locks.get(key);
    if (l && l.owner === from.user.id) {
      this.locks.delete(key);
      this.broadcast({ t: "lock_released", key });
    }
  }

  private releaseProjectLocks(): void {
    const keys = [...this.locks.keys()];
    this.locks.clear();
    // Existing native clients clear local ownership through individual release messages.
    for (const key of keys) this.broadcast({ t: "lock_released", key });
  }

  private activeLock(key: UnitKey, now: number): Lock | undefined {
    const lock = this.locks.get(key);
    if (lock && now - lock.at >= LOCK_TTL_MS) {
      this.locks.delete(key);
      this.broadcast({ t: "lock_released", key });
      return undefined;
    }
    return lock;
  }

  // The periodic sweep also notifies peers when no edit touches the expired key.
  sweepLocks(now = Date.now()): void {
    for (const key of this.locks.keys()) this.activeLock(key, now);
  }

  // Refresh a lock's timestamp while its owner keeps editing (op heartbeat).
  touchLocksOf(userId: string, now = Date.now()): void {
    for (const [key, l] of this.locks) {
      if (l.owner === userId && this.activeLock(key, now)) l.at = now;
    }
  }

  remove(ws: WebSocket): void {
    const client = this.clientOf(ws);
    if (!client) return;
    this.clients.delete(client);
    this.builds.delete(client);

    // release this user's locks
    for (const [key, l] of this.locks) {
      if (l.owner === client.user.id) {
        this.locks.delete(key);
        this.broadcast({ t: "lock_released", key });
      }
    }

    // leader leaving ends the session (handoff is a later feature)
    if (client.user.id === this.leaderId) {
      for (const c of this.clients) {
        send(c.ws, { t: "leader_left" });
        c.ws.close(4000, "leader_left");
      }
      this.clients.clear();
      this.builds.clear();
    } else {
      const compatibility = this.compatibility();
      this.broadcast({ t: "presence", peers: this.peers(), ...(compatibility ? { compatibility } : {}) });
    }

    if (this.clients.size === 0) {
      this.locks.clear();
      this.flp = null;
      this.finish();
    }
  }

  allClients(): Client[] {
    return [...this.clients];
  }
}
