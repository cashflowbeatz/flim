// flim wire protocol. JSON messages over WebSocket; the `.flp` snapshot is
// carried as base64 in `flp`. Keep this file in sync with the FL client.
import { Compatibility, SessionCompatibility } from "./compatibility";

export type Role = "leader" | "member";

export interface UserInfo {
  id: string;
  name: string;
}

export interface PeerInfo {
  id: string;
  name: string;
  role: Role;
  fl?: string;
}

// A unit key identifies the lockable edit atom (see docs/NETWORKING.md).
//  - parameter:   `param:<recEventId>`
//  - mixer track: `mixtrack:<index>`
//  - channel:     `chan:<flimUid>`
//  - note:        `note:<patternUid>:<noteUid>`   (or `group:<patternUid>:<groupId>`)
//  - clip:        `clip:<flimUid>`
//  - pattern:     `pat:<flimUid>`
export type UnitKey = string;

// An op is opaque to the server: it only orders and relays it. The FL client
// defines/interprets the payload. `key` (optional) ties the op to a lock unit.
// `samples` (optional, project.load / plug.state / chan.add) lists the sample files the op's
// data uses: [{p: base64 UTF-16 path, h: sha256 hex, s: bytes}], fetched from /blob/<h> (blobs.ts).
export interface Op {
  kind: string;
  key?: UnitKey;
  [k: string]: unknown;
}

export function validOp(value: unknown): value is Op {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const op = value as Record<string, unknown>;
  if (typeof op.kind !== "string" || !op.kind || op.kind.length > 64 ||
      (op.key !== undefined && (typeof op.key !== "string" || !op.key || op.key.length > 256))) return false;

  // The native client searches serialized control fields. A nested kind or t would
  // let one operation be interpreted as another, bypassing host-only checks.
  const pending: object[] = [op];
  while (pending.length) {
    const current = pending.pop()!;
    if (Array.isArray(current)) {
      for (const child of current) if (child && typeof child === "object") pending.push(child);
      continue;
    }
    for (const [key, child] of Object.entries(current)) {
      if (/["\\\u0000-\u001f]/.test(key) || key === "t" || (key === "kind" && current !== op) ||
          ["source_fl", "host_fl", "compat", "compatibility"].includes(key)) return false;
      if (child && typeof child === "object") pending.push(child);
    }
  }
  return true;
}

// ---- client -> server ----
export type ClientMsg =
  // host: take the first free public slot (01..08) with a new session password. `owner` = the
  // server's owner key (FLIM_OWNER_KEY) asks for the reserved slot 00 instead.
  | { t: "host"; user: UserInfo; password: string; owner?: string; compat?: Compatibility }
  | { t: "join"; user: UserInfo; session: string; password: string; compat?: Compatibility } // session = slot id ("03")
  | { t: "snapshot"; flp: string } // leader seeds/updates the project blob
  | { t: "project_swap"; flp: string } // leader only; replaces the project
  | { t: "op"; op: Op }
  | { t: "lock_acquire"; key: UnitKey }
  | { t: "lock_release"; key: UnitKey }
  | { t: "bye" };

// ---- server -> client ----
export type ServerMsg =
  // blob: this session's key for the sample store (header x-flim-key, see blobs.ts)
  // ends_in: ms until the session's time limit ends it (0 = no limit, slot 00)
  | { t: "welcome"; session: string; role: Role; you: UserInfo; peers: PeerInfo[]; blob: string; ends_in: number; compatibility?: SessionCompatibility }
  | { t: "snapshot"; flp: string | null; epoch: number }
  | { t: "op"; seq: number; from: string; source_fl?: string; op: Op }
  | { t: "lock"; key: UnitKey; granted: boolean; owner: string | null }
  | { t: "lock_state"; locks: Array<{ key: UnitKey; owner: string }> }
  | { t: "lock_released"; key: UnitKey }
  | { t: "project_swap"; flp: string; epoch: number } // members: show resync notice, reload
  | { t: "presence"; peers: PeerInfo[]; compatibility?: SessionCompatibility }
  | { t: "leader_left" } // session is ending
  // codes: bad_json, bad_password, bad_owner, owner_required, no_session, full, in_use, id_in_use, session_full,
  // bad_request, too_many_attempts, too_many_connections, cooldown (+retry_in ms), idle / time_up
  // (session closed; time_up + cooldown ms), not_joined, not_leader, locked, unknown
  | { t: "error"; code: string; msg: string; retry_in?: number; cooldown?: number };

export const LOCK_TTL_MS = 30_000; // safety auto-release if a client never releases
export const PING_INTERVAL_MS = 15_000;
