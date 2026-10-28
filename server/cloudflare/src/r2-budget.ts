import { STORE_MAX } from "./policy";

export const R2_PUTS_MAX = 20_000;
export const R2_GETS_MAX = 100_000;
export interface R2LimitsEnv {
  R2_STORAGE_LIMIT_BYTES?: string;
  R2_MONTHLY_UPLOAD_LIMIT?: string;
  R2_MONTHLY_DOWNLOAD_LIMIT?: string;
}

// A missing setting uses the default. An invalid setting disables that resource.
function limit(value: string | undefined, ceiling: number): number {
  if (value === undefined) return ceiling;
  if (!/^\d+$/.test(value)) return 0;
  const n = Number(value);
  return Number.isSafeInteger(n) && n >= 0 && n <= ceiling ? n : 0;
}

export function r2Limits(env: R2LimitsEnv) {
  return {
    bytes: limit(env.R2_STORAGE_LIMIT_BYTES, STORE_MAX),
    uploads: limit(env.R2_MONTHLY_UPLOAD_LIMIT, R2_PUTS_MAX),
    downloads: limit(env.R2_MONTHLY_DOWNLOAD_LIMIT, R2_GETS_MAX),
  };
}

export function r2Period(now = new Date()) {
  return {
    month: now.toISOString().slice(0, 7),
    resetsAt: new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth() + 1, 1)).toISOString(),
  };
}
