// Sample file store: content-addressed by sha256, so a file is uploaded once per session no
// matter how many projects or channel states reference it. Plain HTTP next to the WebSocket:
//   POST /blobs/missing   body: sha256 hex per line  -> the ones not stored, one per line
//   HEAD /blob/<sha256>   200 + content-length | 404
//   GET  /blob/<sha256>   the file
//   PUT  /blob/<sha256>   store (streamed to disk; rejected if the content doesn't hash to <sha256>)
// Auth: header x-flim-key = the session's blob key (sent in `welcome`). Every session has its own
// folder under FLIM_BLOB_DIR (default: <tmp>/flim-blobs), deleted when the session ends: a
// session can neither see, fetch nor evict another session's files. Per session the oldest files
// are evicted above FLIM_BLOB_SESSION_MAX bytes; uploads are refused (507) while all sessions
// together hold FLIM_BLOB_STORE_MAX. On Fly that disk resets when the machine stops: fine,
// clients re-upload what is missing.
import crypto from "crypto";
import fs from "fs";
import http from "http";
import os from "os";
import path from "path";
import { limits } from "./config";

const BASE = path.resolve(process.env.FLIM_BLOB_DIR ?? path.join(os.tmpdir(), "flim-blobs"));
const ROOT = path.join(BASE, "sessions-v1");
const FILE_MAX = limits.blobFile;
const SESSION_MAX = limits.blobSession;
const STORE_MAX = limits.blobStore;
const HEX = /^[0-9a-f]{64}$/;
let reservedBytes = 0;

// Only remove our generated session directories, never the configured parent or unrelated files.
fs.mkdirSync(ROOT, { recursive: true });
for (const entry of fs.readdirSync(ROOT, { withFileTypes: true })) {
  if (/^[0-9a-f]{32}$/.test(entry.name) && entry.isDirectory() && !entry.isSymbolicLink()) {
    fs.rmSync(path.join(ROOT, entry.name), { recursive: true, force: true });
  }
}

// The session's folder. Named by a hash of its key, so it can't be guessed from anything sent.
export function blobDir(blobKey: string): string {
  return path.join(ROOT, crypto.createHash("sha256").update(blobKey).digest("hex").slice(0, 32));
}

// Session ended: its files go with it.
export function dropBlobs(blobKey: string): void {
  fs.rm(blobDir(blobKey), { recursive: true, force: true, maxRetries: 5, retryDelay: 100 }, () => {});
}

function dirSize(dir: string): number {
  let total = 0;
  try {
    for (const name of fs.readdirSync(dir)) {
      if (!HEX.test(name)) continue; // In-flight uploads are counted by their reserved length.
      try {
        total += fs.statSync(path.join(dir, name)).size;
      } catch {
        /* raced with a delete */
      }
    }
  } catch {
    /* no folder yet */
  }
  return total;
}

function storeSize(): number {
  let total = 0;
  for (const d of fs.readdirSync(ROOT)) total += dirSize(path.join(ROOT, d));
  return total;
}

// Keep one session's folder under SESSION_MAX: drop its least recently used files.
function evict(DIR: string, committed: string): void {
  let entries: { p: string; size: number; t: number }[] = [];
  for (const name of fs.readdirSync(DIR)) {
    if (!HEX.test(name)) continue;
    try {
      const st = fs.statSync(path.join(DIR, name));
      entries.push({ p: path.join(DIR, name), size: st.size, t: st.atimeMs });
    } catch {
      /* raced with a delete */
    }
  }
  let total = entries.reduce((a, e) => a + e.size, 0);
  if (total <= SESSION_MAX) return;
  entries = entries.sort((a, b) => a.t - b.t);
  for (const e of entries) {
    if (total <= SESSION_MAX) break;
    if (e.p === committed) continue; // A successful PUT must survive its own eviction pass.
    try {
      fs.unlinkSync(e.p);
      total -= e.size;
      console.log(`[blob] evicted ${path.basename(e.p).slice(0, 12)} (${e.size} B)`);
    } catch {
      /* ignore */
    }
  }
}

function readBody(req: http.IncomingMessage, max: number): Promise<string | null> {
  return new Promise((resolve) => {
    let n = 0;
    const parts: Buffer[] = [];
    let tooLarge = false;
    req.on("data", (c: Buffer) => {
      if (tooLarge) return;
      n += c.length;
      if (n > max) {
        tooLarge = true;
        parts.length = 0;
        resolve(null);
      } else parts.push(c);
    });
    req.on("end", () => resolve(tooLarge ? null : Buffer.concat(parts).toString("utf8")));
    req.on("error", () => resolve(null));
  });
}

// Returns true if the request was a blob request (handled). `sessionKeyOk` says whether the
// x-flim-key belongs to a running session.
export function handleBlob(
  req: http.IncomingMessage,
  res: http.ServerResponse,
  sessionKeyOk: (key: string) => boolean,
): boolean {
  const url = (req.url ?? "").split("?")[0];
  const m = /^\/blob\/([0-9a-f]{64})$/.exec(url);
  const isMissing = url === "/blobs/missing";
  if (!m && !isMissing) return false;
  res.setHeader("cache-control", "private, no-store");
  const key = String(req.headers["x-flim-key"] ?? "");
  if (!HEX.test(key) || !sessionKeyOk(key)) {
    res.writeHead(401);
    res.end();
    req.resume();
    return true;
  }
  const DIR = blobDir(key);
  const file = (hex: string) => path.join(DIR, hex);
  const exists = (hex: string): fs.Stats | null => {
    try {
      return fs.statSync(file(hex));
    } catch {
      return null;
    }
  };

  if (isMissing) {
    if (req.method !== "POST") {
      res.writeHead(405);
      res.end();
      return true;
    }
    readBody(req, 1024 * 1024).then((body) => {
      if (res.destroyed) return;
      if (!sessionKeyOk(key)) {
        res.writeHead(410);
        res.end();
        return;
      }
      if (body === null) {
        res.writeHead(413);
        res.end();
        return;
      }
      const missing = [...new Set(body.split(/\s+/))]
        .filter((h) => HEX.test(h) && !exists(h));
      res.writeHead(200, { "content-type": "text/plain" });
      res.end(missing.join("\n"));
    });
    return true;
  }

  const hex = m![1];
  if (req.method === "HEAD" || req.method === "GET") {
    const respond = (code: number) => {
      if (res.destroyed) return;
      res.writeHead(code);
      res.end();
    };
    fs.open(file(hex), "r", (err, fd) => {
      if (err) return respond(err.code === "ENOENT" ? 404 : 500);
      const close = () => fs.close(fd, () => {});
      if (res.destroyed) return close();
      if (!sessionKeyOk(key)) { close(); return respond(410); }
      fs.fstat(fd, (err, st) => {
        if (res.destroyed) return close();
        if (!sessionKeyOk(key)) { close(); return respond(410); }
        if (err || !st.isFile()) { close(); return respond(err ? 500 : 404); }
        // The descriptor pins the bytes across cache eviction before streaming begins.
        res.writeHead(200, { "content-type": "application/octet-stream", "content-length": st.size });
        if (req.method === "HEAD") { close(); res.end(); return; }
        fs.utimes(file(hex), new Date(), st.mtime, () => {}); // LRU: recently fetched files stay
        const input = fs.createReadStream(file(hex), { fd, autoClose: true });
        input.on("error", () => res.destroy());
        res.on("close", () => input.destroy());
        input.pipe(res);
      });
    });
    return true;
  }

  if (req.method === "PUT") {
    const len = Number(req.headers["content-length"] ?? -1);
    if (!Number.isSafeInteger(len) || len < 0 || len > Math.min(FILE_MAX, SESSION_MAX)) {
      res.writeHead(413);
      res.end();
      req.resume();
      return true;
    }
    if (exists(hex)) {
      res.writeHead(200);
      res.end();
      req.resume();
      return true;
    }
    if (storeSize() + reservedBytes + len > STORE_MAX) {
      console.log(`[blob] store full: refused ${hex.slice(0, 12)} (${len} B)`);
      res.writeHead(507);
      res.end();
      req.resume();
      return true;
    }
    try {
      fs.mkdirSync(DIR, { recursive: true });
    } catch {
      res.writeHead(500);
      res.end();
      req.resume();
      return true;
    }
    reservedBytes += len;
    let reserved = true;
    const releaseReservation = () => {
      if (!reserved) return;
      reserved = false;
      reservedBytes -= len;
    };
    const tmp = path.join(DIR, `.${hex}.${process.pid}.${Math.random().toString(36).slice(2)}`);
    const out = fs.createWriteStream(tmp);
    const hash = crypto.createHash("sha256");
    let got = 0;
    let failed = false;
    let completed = false;
    const fail = (code: number) => {
      if (failed || completed) return;
      failed = true;
      req.unpipe(out);
      const cleanup = () => fs.unlink(tmp, () => releaseReservation());
      if (out.closed) cleanup();
      else out.once("close", cleanup);
      out.destroy();
      req.resume();
      if (!res.headersSent && !res.destroyed) {
        res.writeHead(code);
        res.end();
      }
    };
    req.on("data", (c: Buffer) => {
      if (failed) return;
      got += c.length;
      if (got > len) return fail(400);
      hash.update(c);
    });
    req.on("error", () => fail(400));
    req.on("aborted", () => fail(400));
    res.on("close", () => { if (!res.writableFinished) fail(400); });
    req.pipe(out);
    out.on("error", () => fail(500));
    out.on("finish", () => {
      if (failed) return;
      if (got !== len || hash.digest("hex") !== hex) {
        console.log(`[blob] rejected ${hex.slice(0, 12)}: content does not match`);
        return fail(400);
      }
      if (!sessionKeyOk(key)) return fail(410); // the session ended during the upload
      fs.rename(tmp, file(hex), (err) => {
        if (err) return fail(500);
        if (!sessionKeyOk(key)) {
          fs.unlink(file(hex), () => releaseReservation());
          return fail(410);
        }
        completed = true;
        releaseReservation();
        console.log(`[blob] stored ${hex.slice(0, 12)} (${got} B)`);
        // A disconnect after rename must not remove a valid blob another uploader also uses.
        if (!failed && !res.destroyed) {
          res.writeHead(201);
          res.end();
        }
        try {
          evict(DIR, file(hex));
        } catch {
          /* ignore */
        }
      });
    });
    return true;
  }

  res.writeHead(405);
  res.end();
  return true;
}
