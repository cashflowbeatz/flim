export const COMPATIBILITY_PROTOCOL = 1;
export const SHARED_WIRE = "flim-25-26-v1";

export interface Compatibility {
  protocol: 1;
  fl: string;
  wire: typeof SHARED_WIRE;
}

export interface SessionCompatibility {
  protocol: 1;
  host_fl: string;
  wire: typeof SHARED_WIRE;
  mixed: boolean;
}

export function flVersion(value: unknown): number[] | null {
  if (typeof value !== "string" || !/^(?:0|[1-9]\d{0,4})(?:\.(?:0|[1-9]\d{0,4})){3}$/.test(value)) return null;
  const parts = value.split(".").map(Number);
  return parts.join(".") === value && (parts[0] === 25 || parts[0] === 26 || value === "24.2.2.4597") &&
    parts.every((part) => part <= 65535) ? parts : null;
}

export function validCompatibility(value: unknown): value is Compatibility {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const descriptor = value as Record<string, unknown>;
  return Object.keys(descriptor).length === 3 && descriptor.protocol === COMPATIBILITY_PROTOCOL &&
    descriptor.wire === SHARED_WIRE && flVersion(descriptor.fl) !== null;
}

export function compareFlVersions(a: string, b: string): number {
  const left = flVersion(a), right = flVersion(b);
  if (!left || !right) throw new Error("Invalid FL version comparison");
  for (let i = 0; i < left.length; i++) if (left[i] !== right[i]) return left[i] - right[i];
  return 0;
}
