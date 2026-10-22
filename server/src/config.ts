function integer(name: string, fallback: number, min = 1, max = Number.MAX_SAFE_INTEGER): number {
  const raw = process.env[name];
  const value = raw === undefined ? fallback : Number(raw);
  if ((raw !== undefined && !/^\d+$/.test(raw.trim())) ||
      !Number.isSafeInteger(value) || value < min || value > max) {
    throw new Error(`${name} must be an integer from ${min} to ${max}`);
  }
  return value;
}

// Validate every limit before startup can clear a cache or open a listener.
export const limits = {
  port: integer("PORT", 8080, 0, 65535),
  blobFile: integer("FLIM_BLOB_FILE_MAX", 2000 * 1024 * 1024),
  blobSession: integer("FLIM_BLOB_SESSION_MAX", 2500 * 1024 * 1024),
  blobStore: integer("FLIM_BLOB_STORE_MAX", 5 * 1024 * 1024 * 1024),
  connectionsPerIp: integer("FLIM_MAX_CONNS_PER_IP", 12),
  idleMs: integer("FLIM_IDLE_MS", 5 * 60_000, 0),
  sessionMs: integer("FLIM_SESSION_MS", 15 * 60_000, 0),
  cooldownMs: integer("FLIM_COOLDOWN_MS", 5 * 60_000, 0),
};
