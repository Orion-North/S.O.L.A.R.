const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('solarDesktop', {
  requestRobot: (payload) => ipcRenderer.invoke('robot-request', payload),
  readConfig: () => ipcRenderer.invoke('config-read'),
  writeConfig: (nextConfig) => ipcRenderer.invoke('config-write', nextConfig),
  appInfo: () => ipcRenderer.invoke('app-info'),
});
