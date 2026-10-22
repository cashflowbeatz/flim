import http from "http";
import { isIP } from "net";

export type ProxyMode = "none" | "fly" | "cloudflare";

export function proxyMode(value = process.env.FLIM_PROXY ?? "none"): ProxyMode {
  if (value === "none" || value === "fly" || value === "cloudflare") return value;
  throw new Error("FLIM_PROXY must be none, fly, or cloudflare");
}

export function clientIp(req: http.IncomingMessage, mode: ProxyMode): string {
  const remote = req.socket.remoteAddress ?? "?";
  const loopback = remote === "127.0.0.1" || remote === "::1" || remote === "::ffff:127.0.0.1";
  // Cloudflare mode is for a local cloudflared connector, with the origin bound to loopback.
  const header = mode === "fly" ? "fly-client-ip" : mode === "cloudflare" && loopback ? "cf-connecting-ip" : null;
  const forwarded = header ? req.headers[header] : undefined;
  const address = typeof forwarded === "string" && isIP(forwarded) ? forwarded : remote;
  return address.startsWith("::ffff:") && isIP(address.slice(7)) === 4 ? address.slice(7) : address;
}
