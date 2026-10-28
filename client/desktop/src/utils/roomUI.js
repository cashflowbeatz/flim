/** @param {string} name */
export function validateName(name) {
  const normalized = name.normalize('NFC');
  if (!/[\p{L}\p{N}]/u.test(normalized.replace(/[\uD800-\uDFFF]/g, ''))) return 'Enter a name with at least one letter or number.';
  if (normalized.length > 24) return 'Use up to 24 characters for your name.';
  return '';
}

/** @param {string} code */
export function validateCode(code) {
  return /^\d{8}$/.test(normalizeCode(code).replace(/[ -]/g, '')) ? '' : 'Enter all eight digits of the room number.';
}

/** @param {string} code */
export function normalizeCode(code) {
  return code.normalize('NFKC').replace(/[\u0660-\u0669\u06f0-\u06f9]/g, digit => String(digit.charCodeAt(0) & 15))
    .replace(/\s/g, ' ').replace(/[\u2010-\u2014\u2212]/g, '-');
}

/** @param {string} code */
export function cleanCodeInput(code) { return normalizeCode(code).replace(/[^\d -]/g, ''); }

/** @param {string} code */
export function formatCode(code) { return `${code.slice(0, 4)} ${code.slice(4)}`; }

const collator = new Intl.Collator(undefined, {numeric: true, sensitivity: 'base'});
const segmenter = new Intl.Segmenter(undefined, {granularity: 'grapheme'});

/** @param {string} name */
export function initialFor(name) {
  return segmenter.segment(name.trim())[Symbol.iterator]().next().value?.segment.toLocaleUpperCase() || '?';
}

/** @param {import('../types').Room['peers']} peers */
export function sortedPeers(peers) {
  return [...peers].sort((a, b) => Number(b.role === 'host') - Number(a.role === 'host') || collator.compare(a.name, b.name) || a.id.localeCompare(b.id));
}

/** @param {import('../types').Room['status']} status */
export function statusLabel(status) {
  return status === 'ended' ? 'Room closed' : status === 'active' ? 'Room is open' : 'Getting ready';
}

/** @param {import('../types').Room['status']} status @param {boolean} online @param {boolean} stale */
export function connectionStatus(status, online, stale) {
  if (status === 'ended') return {label: 'Room closed', tone: 'ended'};
  if (!online) return {label: 'Offline', tone: 'waiting'};
  if (stale) return {label: 'Status unavailable', tone: 'waiting'};
  return {label: statusLabel(status), tone: status};
}

/** @param {number | null | undefined} timestamp @param {number} now */
export function updatedLabel(timestamp, now) {
  if (timestamp == null || !Number.isFinite(timestamp)) return '';
  const seconds = Math.max(0, Math.floor((now - timestamp) / 1000));
  if (seconds < 10) return 'People updated just now';
  if (seconds < 60) return `People updated ${seconds} seconds ago`;
  const minutes = Math.floor(seconds / 60);
  return `People updated ${minutes} ${minutes === 1 ? 'minute' : 'minutes'} ago`;
}
