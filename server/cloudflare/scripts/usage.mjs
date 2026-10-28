import { readFile } from "node:fs/promises";

try {
  if (!process.env.FLIM_RELAY || !process.env.FLIM_HOST_KEY_FILE) throw new Error("Set FLIM_RELAY and FLIM_HOST_KEY_FILE before running this command.");
  const relay = new URL(process.env.FLIM_RELAY);
  if (relay.protocol !== "https:" || relay.username || relay.password) throw new Error("FLIM_RELAY must be an HTTPS URL without credentials.");
  const owner = (await readFile(process.env.FLIM_HOST_KEY_FILE, "utf8")).trim();
  const response = await fetch(new URL("/usage", relay), { headers: { "x-flim-owner": owner }, signal: AbortSignal.timeout(20000), redirect: "error" });
  if (!response.ok) throw new Error(`Usage request failed (HTTP ${response.status}). Check the relay address and private hosting-key file.`);
  const usage = await response.json();
  console.log(`Temporary storage: ${(usage.storage.bytes / 1048576).toFixed(2)} / ${(usage.storage.limit_bytes / 1048576).toFixed(2)} MiB; ${usage.storage.files} / ${usage.storage.limit_files} files`);
  console.log(`Monthly upload attempts: ${usage.uploads.used} / ${usage.uploads.limit}${usage.uploads.blocked ? " — uploads paused" : ""}`);
  console.log(`Monthly download attempts: ${usage.downloads.used} / ${usage.downloads.limit}${usage.downloads.blocked ? " — downloads paused" : ""}`);
  console.log(`Monthly counters reset at ${usage.resets_at}`);
  console.log(usage.scope);
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
