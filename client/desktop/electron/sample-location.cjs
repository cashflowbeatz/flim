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
    !value.slice(2).includes(':') && !value.split(/[\\/]/).some(part => part && /[. ]$/.test(part));
}

async function checkDirectory(directory, {io = fs, create = false, writable = true} = {}) {
  if (!validDirectory(directory)) throw new Error('Choose a local or network folder with a path of 180 characters or fewer.');
  if (create) await io.mkdir(directory, {recursive: true});
  const resolved = await io.realpath(directory);
  if (!validDirectory(resolved)) throw new Error('The actual folder path is too long or cannot be used by FL Studio. Choose a shorter location.');
  if (!(await io.stat(resolved)).isDirectory()) throw new Error('Choose a folder for received files.');
  if (writable) {
    const probe = path.join(resolved, `.flim-write-${randomUUID()}.tmp`);
    let handle, owned = false;
    try {
      handle = await io.open(probe, 'wx', 0o600); owned = true;
      await handle.writeFile('flim'); await handle.sync();
    } finally {
      try { if (handle) await handle.close(); }
      finally { if (owned) await io.unlink(probe); }
    }
  }
  return resolved;
}

module.exports = {checkDirectory, validDirectory, MAX_DIRECTORY};
