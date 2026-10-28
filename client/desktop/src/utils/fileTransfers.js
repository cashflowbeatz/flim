/** @param {number} bytes */
export function fileSize(bytes) {
  if (!Number.isFinite(bytes) || bytes < 0) return 'Size unavailable';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes >= 1024 ** 4) return `${(bytes / 1024 ** 4).toFixed(1)} TB`;
  if (bytes >= 1024 ** 3) return `${(bytes / 1024 ** 3).toFixed(1)} GB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

/** @param {import('../types').FileTransfer} file */
export function fileStatus(file) {
  if (file.state === 'ready') return file.direction === 'upload' ? 'Available to the room' : 'Ready on this computer';
  if (file.state === 'checking') return 'Checking file';
  if (file.state === 'blocked') return 'File allowance reached';
  if (file.state === 'waiting') return file.reason === 'local_cache' ? 'Waiting for folder access' : file.reason === 'verification' ? 'Checking again automatically' : 'Waiting to retry automatically';
  if (file.state === 'missing') return 'Source file missing';
  if (file.state === 'uploading') return file.reason === 'retrying' ? 'Retrying upload' : 'Uploading';
  if (file.state === 'downloading') return file.reason === 'retrying' ? 'Retrying download' : 'Downloading';
  return file.reason === 'verification' ? 'Download could not be verified' : file.reason === 'too_large' ? 'Above the file size limit' :
    file.reason === 'empty' ? 'File is empty' : file.reason === 'source_changed' ? 'File changed while reading' : 'Transfer could not finish';
}

/** @param {import('../types').FileTransfer} file */
export function fileProgress(file) { return Number.isFinite(file.bytes) && Number.isFinite(file.done) && file.bytes > 0 ? Math.max(0, Math.min(100, Math.floor(file.done / file.bytes * 100))) : 0; }

/** @param {string} text */
export function fileSearchKey(text) { return text.normalize('NFKD').replace(/\p{M}/gu, '').toLocaleLowerCase().trim(); }

/** @param {import('../types').FileTransfer} file */
export function fileAmount(file) {
  if (!file.bytes && ['checking', 'waiting', 'missing', 'blocked'].includes(file.state)) return 'Size not known yet';
  return ['uploading', 'downloading'].includes(file.state) ? `${fileSize(file.done)} of ${fileSize(file.bytes)} · ${fileProgress(file)}%` : fileSize(file.bytes);
}

/** @param {import('../types').FileTransferState} previous @param {import('../types').FileTransferState} next */
export function reconcileTransfers(previous, next) {
  const known = new Map(previous.items.map(file => [file.id, file]));
  const source = (!next.available || next.stale) && !next.items.length ? previous.items : next.items;
  const items = source.map(file => {
    const old = known.get(file.id);
    return old && ['name', 'direction', 'state', 'reason', 'bytes', 'done', 'updatedAt'].every(key => old[key] === file[key]) ? old : file;
  });
  const retained = source === previous.items && source !== next.items;
  const total = retained ? previous.total : next.total;
  const truncated = retained ? previous.truncated : next.truncated;
  return previous.available === next.available && previous.stale === next.stale && previous.total === total && previous.truncated === truncated && items.length === previous.items.length && items.every((file, i) => file === previous.items[i]) ? previous : {...next, items, ...(total === undefined ? {} : {total}), ...(truncated === undefined ? {} : {truncated})};
}
