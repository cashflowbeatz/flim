'use strict';
const {contextBridge, ipcRenderer} = require('electron');
contextBridge.exposeInMainWorld('peerTransport', {
  event: value => ipcRenderer.invoke('flim:peer-event', value),
  onCommand: callback => ipcRenderer.on('flim:peer-command', (_event, command) => callback(command)),
  reply: (id, ok) => ipcRenderer.send('flim:peer-reply', id, ok)
});
