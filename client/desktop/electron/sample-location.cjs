'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const {randomUUID} = require('node:crypto');

// Leave room for the hash directory, a filename, and the native download suffix.
const MAX_DIRECTORY = 180;
function validDirectory(value) {
  return typeof value === 'string' && value.length > 0 && value.length <= MAX_DIRECTORY &&
    /^(?:[A-Za-z]:[\\/]|\\\\[^\\/]+[\\/][^\\/]+)/.test(value) &&
    !/^[\\/]{2}[?.][\\/]/.test(value) && !/[\x00-\x1f\x7f<>"|?*]/.test(value) &&
    !value.slice(2).includes(':') && value.isWellFormed() && !value.split(/[\\/]/).some(part => part && (/[. ]$/.test(part) || /^(?:CON|PRN|AUX|NUL|COM[1-9\u00b9\u00b2\u00b3]|LPT[1-9\u00b9\u00b2\u00b3])(?:[. ]|$)/i.test(part)));
}

async function checkDirectory(directory, {io = fs, create = false, writable = true} = {}) {
  if (!validDirectory(directory)) throw new Error('Choose a local or network folder with a path of 180 characters or fewer.');
  if (create) await io.mkdir(directory, {recursive: true});
  const resolved = await io.realpath(directory);
  if (!validDirectory(resolved)) throw new Error('The actual folder path is too long or cannot be used by FL Studio. Choose a shorter location.');
  if (!(await io.stat(resolved)).isDirectory()) throw new Error('Choose a folder for received files.');
  if (writable) {
    const probe = path.join(resolved, `.flim-write-${randomUUID()}.tmp`);
    let handle, owned = false, failure;
    try {
      handle = await io.open(probe, 'wx', 0o600); owned = true;
      await handle.writeFile('flim'); await handle.sync();
    } catch (error) { failure = error; }
    finally {
      try { if (handle) await handle.close(); } catch (error) { failure ??= error; }
      try { if (owned) await io.unlink(probe); } catch (error) { failure ??= error; }
    }
    if (failure) throw failure;
  }
  return resolved;
}

function folderError(error, fallback) {
  if (/^(Choose |The actual )/.test(error?.message || '')) return error.message;
  if (error?.code === 'ENOSPC' || error?.code === 'EDQUOT') return 'The samples drive is full or has reached its storage quota. Free some space or choose another folder in Files.';
  if (['ENOENT', 'ENOTDIR', 'ENODEV', 'ENXIO'].includes(error?.code)) return 'The samples folder or its drive is no longer available. Reconnect the drive or choose another folder in Files.';
  if (['EACCES', 'EPERM', 'EROFS'].includes(error?.code)) return 'The samples folder does not allow writing. Check its permissions or choose another folder in Files.';
  return fallback;
}
module.exports = {checkDirectory, validDirectory, folderError, MAX_DIRECTORY};
