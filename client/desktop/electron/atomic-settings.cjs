'use strict';

const path = require('node:path');
const {randomUUID} = require('node:crypto');
const {setTimeout: delay} = require('node:timers/promises');

async function writeSettings(io, destination, text) {
  const temporary = path.join(path.dirname(destination), `.flim-settings-${randomUUID()}.tmp`);
  let handle, owned = false;
  try {
    handle = await io.open(temporary, 'wx', 0o600); owned = true;
    await handle.writeFile(text, 'utf8');
    await handle.sync();
    await handle.close(); handle = null;
    for (let attempt = 0; ; attempt++) {
      try { await io.rename(temporary, destination); owned = false; break; }
      catch (error) {
        if (attempt >= 2 || !['EBUSY', 'EACCES', 'EPERM'].includes(error.code)) throw error;
        await delay(40 * (attempt + 1));
      }
    }
  } finally {
    try { if (handle) await handle.close(); }
    finally { if (owned) await io.unlink(temporary).catch(() => {}); }
  }
}

module.exports = {writeSettings};
