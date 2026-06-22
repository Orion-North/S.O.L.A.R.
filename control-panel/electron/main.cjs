const { app, BrowserWindow, ipcMain, shell } = require('electron');
const { readFileSync, writeFileSync, existsSync } = require('node:fs');
const { join } = require('node:path');

const isDev = Boolean(process.env.SOLAR_CONTROL_DEV_SERVER);
const routePolicy = new Map([
  ['/ping', { minIntervalMs: 250, timeoutMs: 1200, methods: new Set(['GET']) }],
  ['/status', { minIntervalMs: 1000, timeoutMs: 1800, methods: new Set(['GET']) }],
  ['/capture', { minIntervalMs: 150, timeoutMs: 2500, methods: new Set(['GET']) }],
  ['/cmd', { minIntervalMs: 80, timeoutMs: 800, methods: new Set(['GET', 'POST']) }],
  ['/rl', { minIntervalMs: 50, timeoutMs: 800, methods: new Set(['GET', 'POST']) }],
  ['/imu', { minIntervalMs: 50, timeoutMs: 1200, methods: new Set(['GET']) }],
  ['/obs', { minIntervalMs: 50, timeoutMs: 1200, methods: new Set(['GET']) }],
  ['/flash', { minIntervalMs: 250, timeoutMs: 1200, methods: new Set(['GET', 'POST']) }],
  ['/flash/auto', { minIntervalMs: 250, timeoutMs: 1200, methods: new Set(['GET', 'POST']) }],
  ['/estop', { minIntervalMs: 100, timeoutMs: 800, methods: new Set(['GET', 'POST']) }],
  ['/estop/clear', { minIntervalMs: 250, timeoutMs: 1200, methods: new Set(['GET', 'POST']) }],
  ['/charge-rest', { minIntervalMs: 250, timeoutMs: 1200, methods: new Set(['GET', 'POST']) }],
  ['/testseq', { minIntervalMs: 1000, timeoutMs: 1200, methods: new Set(['GET', 'POST']) }],
  ['/settings/get', { minIntervalMs: 1000, timeoutMs: 1800, methods: new Set(['GET']) }],
  ['/settings/set', { minIntervalMs: 1000, timeoutMs: 2200, methods: new Set(['GET', 'POST']) }],
  ['/torque', { minIntervalMs: 250, timeoutMs: 1200, methods: new Set(['GET', 'POST']) }],
  ['/calib', { minIntervalMs: 250, timeoutMs: 1200, methods: new Set(['GET', 'POST']) }],
  ['/test', { minIntervalMs: 80, timeoutMs: 800, methods: new Set(['GET', 'POST']) }],
]);
const lastRouteHit = new Map();

function configPath() {
  return join(app.getPath('userData'), 'control-panel.json');
}

function readConfig() {
  const path = configPath();
  if (!existsSync(path)) return {};
  try {
    return JSON.parse(readFileSync(path, 'utf8'));
  } catch {
    return {};
  }
}

function writeConfig(nextConfig) {
  writeFileSync(configPath(), JSON.stringify(nextConfig, null, 2));
}

function normalizeRobotTarget(target) {
  const trimmed = String(target || '').trim();
  if (!trimmed) throw new Error('Missing robot target');
  const withProtocol = /^https?:\/\//i.test(trimmed) ? trimmed : `http://${trimmed}`;
  const url = new URL(withProtocol);
  if (!['http:', 'https:'].includes(url.protocol)) throw new Error('Unsupported robot target protocol');
  url.hash = '';
  return url.toString().replace(/\/+$/, '');
}

function makeRobotUrl(target, path, params = {}) {
  const route = String(path || '');
  if (!routePolicy.has(route)) throw new Error(`Robot route is not allowed: ${route}`);
  const base = normalizeRobotTarget(target);
  const url = new URL(route, `${base}/`);
  for (const [key, value] of Object.entries(params || {})) {
    if (value !== undefined && value !== null) url.searchParams.set(key, String(value));
  }
  return url;
}

async function requestRobot(_event, payload = {}) {
  const route = String(payload.path || '');
  const policy = routePolicy.get(route);
  if (!policy) return { ok: false, status: 403, text: `Route not allowed: ${route}` };

  const method = String(payload.method || 'GET').toUpperCase();
  if (!policy.methods.has(method)) return { ok: false, status: 405, text: `Method not allowed: ${method}` };

  const now = Date.now();
  const lastHit = lastRouteHit.get(route) || 0;
  const elapsed = now - lastHit;
  if (elapsed < policy.minIntervalMs) {
    return {
      ok: false,
      status: 429,
      text: `Rate limited for ${policy.minIntervalMs - elapsed} ms`,
      rateLimited: true,
      retryAfterMs: policy.minIntervalMs - elapsed,
    };
  }
  lastRouteHit.set(route, now);

  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), Number(payload.timeoutMs || policy.timeoutMs));
  try {
    const url = makeRobotUrl(payload.target, route, payload.params);
    const headers = {};
    if (payload.token) headers['x-solar-token'] = String(payload.token);

    const upstream = await fetch(url, {
      method,
      headers,
      cache: 'no-store',
      signal: controller.signal,
    });

    const contentType = upstream.headers.get('content-type') || '';
    if (payload.responseType === 'blob' || contentType.startsWith('image/')) {
      const buffer = Buffer.from(await upstream.arrayBuffer());
      return {
        ok: upstream.ok,
        status: upstream.status,
        contentType,
        bodyBase64: buffer.toString('base64'),
      };
    }

    return {
      ok: upstream.ok,
      status: upstream.status,
      contentType,
      text: await upstream.text(),
    };
  } catch (error) {
    return {
      ok: false,
      status: controller.signal.aborted ? 408 : 502,
      text: String(error.message || error),
    };
  } finally {
    clearTimeout(timeoutId);
  }
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1440,
    height: 920,
    minWidth: 1080,
    minHeight: 720,
    backgroundColor: '#07090f',
    show: false,
    webPreferences: {
      preload: join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  win.once('ready-to-show', () => win.show());
  win.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: 'deny' };
  });

  if (isDev) {
    win.loadURL(process.env.SOLAR_CONTROL_DEV_SERVER);
    win.webContents.openDevTools({ mode: 'detach' });
  } else {
    win.loadFile(join(__dirname, '..', 'dist', 'index.html'));
  }
}

ipcMain.handle('robot-request', requestRobot);
ipcMain.handle('config-read', () => readConfig());
ipcMain.handle('config-write', (_event, nextConfig) => {
  writeConfig(nextConfig || {});
  return true;
});
ipcMain.handle('app-info', () => ({
  version: app.getVersion(),
  desktopProxy: true,
}));

app.whenReady().then(() => {
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
