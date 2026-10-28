'use strict';

const { contextBridge, ipcRenderer } = require('electron');

async function invoke(channel, input) {
  try { return await ipcRenderer.invoke(`flim:${channel}`, input); }
  catch (error) {
    const message = typeof error?.message === 'string' ? error.message.replace(/^Error invoking remote method 'flim:[a-z-]+': (?:Error: )?/, '') : 'That step could not be completed. Please try again.';
    throw new Error(message);
  }
}

contextBridge.exposeInMainWorld('flimDesktop', Object.freeze({
  getState: () => invoke('get-state'),
  getFileTransfers: () => invoke('get-file-transfers'),
  openSampleFolder: () => invoke('open-sample-folder'),
  getSampleLocation: () => invoke('get-sample-location'),
  chooseSampleLocation: () => invoke('choose-sample-location'),
  resetSampleLocation: () => invoke('reset-sample-location'),
  savePreferences: input => invoke('save-preferences', input),
  chooseInstall: currentInstallId => invoke('choose-install', currentInstallId),
  rescanInstalls: currentInstallId => invoke('rescan-installs', currentInstallId),
  createRoom: input => invoke('create-room', input),
  joinRoom: input => invoke('join-room', input),
  launchFL: () => invoke('launch-fl'),
  refreshRoom: options => invoke('refresh-room', options),
  leaveRoom: () => invoke('leave-room'),
  copyInvite: () => invoke('copy-invite'),
  pasteRoomCode: () => invoke('paste-room-code'),
  openLogs: () => invoke('open-logs'),
  reloadControls: () => invoke('reload-controls'),
  reportError: input => { ipcRenderer.send('flim:renderer-error', {message: String(input?.message ?? '').slice(0, 2048), stack: String(input?.stack ?? '').slice(0, 4096)}); },
  fitWindow: (height, width = 400) => { if (Number.isFinite(height) && height > 0 && height <= 8192 && [400, 456, 680, 756].includes(width)) ipcRenderer.send('flim:fit-window', height, width); },
}));
