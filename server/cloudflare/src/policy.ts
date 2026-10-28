import { compareFlVersions, validCompatibility, type Compatibility } from "../../src/compatibility";
import { validOp, type Op } from "../../src/protocol";

export const INLINE_MAX = 128 * 1024;
export const MESSAGE_MAX = 64 * 1024 * 1024;
export const FILE_MAX = 90_000_000;
export const STORE_MAX = 512 * 1024 * 1024;
export const FILE_COUNT_MAX = 2000;
export const HOST_ONLY = new Set(["project.load", "plug.full", "notes.full", "state.full", "portable.start"]);
export const SHARED = new Set(["project.load", "plug.full", "notes.full", "state.full", "state.req", "plug.state", "chan.add", "chan.del", "notes.set", "clips.set", "meta.set", "pat.op", "focus", "cur", "fx.clear", "chan.order", "param.set", "portable.start"]);
export const OPAQUE = new Set(["plug.state", "chan.add"]);
export interface Stored { t: "stored"; type: "op" | "snapshot" | "project_swap"; h: string; s: number; kind?: string; key?: string; for?: string; delivery?: "snapshot" | "project_swap" }
export function hash(value: unknown): value is string { return typeof value === "string" && /^[a-f0-9]{64}$/.test(value); }
export function key(value: unknown): value is string { return typeof value === "string" && /^[^"\\\x00-\x1f]{1,256}$/.test(value); }
export function stored(value: unknown): value is Stored {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const m = value as Record<string, unknown>;
  if (Object.keys(m).some(k => !["t", "type", "h", "s", "kind", "key", "for"].includes(k)) || m.t !== "stored" || !hash(m.h) ||
      !Number.isSafeInteger(m.s) || (m.s as number) < 1 || (m.s as number) > MESSAGE_MAX) return false;
  if (m.type === "op") return typeof m.kind === "string" && SHARED.has(m.kind) && (m.key === undefined || key(m.key)) && (m.for === undefined || typeof m.for === "string" && /^[A-Za-z0-9_-]{1,64}$/.test(m.for));
  return (m.type === "snapshot" || m.type === "project_swap") && m.kind === undefined && m.key === undefined && m.for === undefined;
}
export function admission(build: unknown, host: Compatibility, portable = false): string | null {
  if (!validCompatibility(build)) return "Update flim: an FL 25/26 compatibility descriptor is required.";
  return !portable && compareFlVersions(build.fl, host.fl) < 0 ? "Update flim on every computer to join a newer host automatically." : null;
}
export function operation(op: Op, leader: boolean, build: Compatibility, host: Compatibility, mixed: boolean, portable = false): string | null {
  if (!validOp(op) || !SHARED.has(op.kind) || (op.key !== undefined && !key(op.key))) return "Malformed or unsupported operation.";
  if (HOST_ONLY.has(op.kind) && !leader) return "Only the host can replace the project or full state.";
  if (op.for !== undefined && (typeof op.for !== "string" || !/^[A-Za-z0-9_-]{1,64}$/.test(op.for) || !leader)) return "Only the host can direct a shared baseline to a participant.";
  if (op.kind === "portable.start" && !portable) return "This room has not negotiated portable sync.";
  if (!portable && mixed && OPAQUE.has(op.kind) && build.fl !== host.fl) return "The host's FL build must send plugin presets and create channels in a mixed-build session.";
  if (portable && op.kind === "clips.set" && compareFlVersions(build.fl, host.fl) < 0) return "The older participant shares notes and controls; its unsupported playlist cannot replace the host's playlist.";
  return null;
}
