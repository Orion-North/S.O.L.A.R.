const state = {
  target: localStorage.getItem('solar_mobile_target') || defaultTarget(),
  token: localStorage.getItem('solar_mobile_token') || '',
  live: false,
  moving: null,
  flash: false,
  heartbeatTimer: null,
  frameTimer: null,
  lastLatency: null,
};

const cameraFeed = document.getElementById('camera-feed');
const feedEmpty = document.getElementById('feed-empty');
const connectionPill = document.getElementById('connection-pill');
const liveButton = document.getElementById('live-button');
const estopButton = document.getElementById('estop-button');
const stopButton = document.getElementById('stop-button');
const speedSlider = document.getElementById('speed-slider');
const speedValue = document.getElementById('speed-value');
const modeReadout = document.getElementById('mode-readout');
const latencyReadout = document.getElementById('latency-readout');
const networkReadout = document.getElementById('network-readout');
const settingsButton = document.getElementById('settings-button');
const settingsSheet = document.getElementById('settings-sheet');
const targetInput = document.getElementById('target-input');
const tokenInput = document.getElementById('token-input');
const connectButton = document.getElementById('connect-button');

targetInput.value = state.target;
tokenInput.value = state.token;
speedValue.textContent = Number(speedSlider.value).toFixed(1);

function defaultTarget() {
  if (location.protocol.startsWith('http')) return location.origin;
  return 'http://solar.local';
}

function robotUrl(path, params = {}) {
  const base = /^https?:\/\//i.test(state.target) ? state.target.replace(/\/+$/, '') : `http://${state.target}`;
  const query = new URLSearchParams(params);
  if (state.token) query.set('token', state.token);
  return `${base}${path}${query.toString() ? `?${query}` : ''}`;
}

async function requestRobot(path, params = {}, options = {}) {
  const startedAt = performance.now();
  const response = await fetch(robotUrl(path, params), options);
  state.lastLatency = Math.round(performance.now() - startedAt);
  renderLatency();
  return response;
}

function renderLatency() {
  latencyReadout.textContent = state.lastLatency == null ? '--' : `${state.lastLatency} ms`;
}

function setOnline(online) {
  connectionPill.textContent = online ? 'Online' : 'Offline';
  connectionPill.classList.toggle('online', online);
}

async function connect() {
  try {
    const response = await requestRobot('/ping', {}, { signal: AbortSignal.timeout(2500) });
    setOnline(response.ok);
    if (response.ok) await refreshStatus();
  } catch {
    setOnline(false);
  }
}

async function refreshStatus() {
  try {
    const response = await requestRobot('/status', {}, { signal: AbortSignal.timeout(2500) });
    if (!response.ok) throw new Error('status failed');
    const status = await response.json();
    modeReadout.textContent = String(status.mode || '--');
    networkReadout.textContent = status.wifi_mode ? String(status.wifi_mode).replaceAll('_', ' ') : '--';
    setOnline(true);
  } catch {
    setOnline(false);
  }
}

function requestFrame() {
  if (!state.live) return;
  cameraFeed.src = robotUrl('/capture', { t: Date.now() });
}

function scheduleFrame(delay) {
  clearTimeout(state.frameTimer);
  if (state.live) state.frameTimer = setTimeout(requestFrame, delay);
}

cameraFeed.addEventListener('load', () => {
  feedEmpty.hidden = true;
  scheduleFrame(180);
});

cameraFeed.addEventListener('error', () => {
  scheduleFrame(500);
});

function setLive(enabled) {
  state.live = enabled;
  liveButton.textContent = enabled ? 'Stop camera' : 'Start camera';
  feedEmpty.textContent = enabled ? 'Connecting camera' : 'Camera paused';
  feedEmpty.hidden = false;
  clearTimeout(state.frameTimer);
  if (enabled) requestFrame();
}

function movementFor(direction) {
  if (direction === 'forward') return { vx: 1, wz: 0 };
  if (direction === 'backward') return { vx: -1, wz: 0 };
  if (direction === 'left') return { vx: 0, wz: 1 };
  if (direction === 'right') return { vx: 0, wz: -1 };
  return { vx: 0, wz: 0 };
}

async function drive(direction) {
  state.moving = direction;
  document.querySelectorAll('.drive-btn').forEach((button) => {
    button.classList.toggle('active', button.dataset.drive === direction);
  });

  const movement = movementFor(direction);
  await requestRobot('/cmd', {
    mode: 'walk',
    vx: movement.vx,
    vy: 0,
    wz: movement.wz,
    speed: Number(speedSlider.value).toFixed(1),
  }).catch(() => setOnline(false));

  clearInterval(state.heartbeatTimer);
  state.heartbeatTimer = setInterval(() => {
    if (state.moving) requestRobot('/ping').catch(() => setOnline(false));
  }, 450);
}

async function stopDrive() {
  state.moving = null;
  clearInterval(state.heartbeatTimer);
  document.querySelectorAll('.drive-btn').forEach((button) => button.classList.remove('active'));
  await requestRobot('/cmd', { mode: 'stand', vx: 0, vy: 0, wz: 0 }).catch(() => setOnline(false));
}

document.querySelectorAll('.drive-btn').forEach((button) => {
  button.addEventListener('pointerdown', (event) => {
    event.preventDefault();
    button.setPointerCapture(event.pointerId);
    drive(button.dataset.drive);
  });
  button.addEventListener('pointerup', (event) => {
    event.preventDefault();
    stopDrive();
  });
  button.addEventListener('pointercancel', stopDrive);
});

stopButton.addEventListener('click', stopDrive);
liveButton.addEventListener('click', () => setLive(!state.live));

estopButton.addEventListener('click', async () => {
  await requestRobot('/estop').catch(() => setOnline(false));
  await stopDrive();
});

speedSlider.addEventListener('input', () => {
  speedValue.textContent = Number(speedSlider.value).toFixed(1);
  if (state.moving) drive(state.moving);
});

document.querySelectorAll('[data-mode]').forEach((button) => {
  button.addEventListener('click', () => requestRobot('/cmd', { mode: button.dataset.mode }).catch(() => setOnline(false)));
});

document.getElementById('flash-button').addEventListener('click', () => {
  state.flash = !state.flash;
  requestRobot('/flash', { state: state.flash ? 1 : 0 }).catch(() => setOnline(false));
});

document.getElementById('stand-button').addEventListener('click', stopDrive);

settingsButton.addEventListener('click', () => settingsSheet.showModal());

connectButton.addEventListener('click', (event) => {
  event.preventDefault();
  state.target = targetInput.value.trim() || defaultTarget();
  state.token = tokenInput.value.trim();
  localStorage.setItem('solar_mobile_target', state.target);
  localStorage.setItem('solar_mobile_token', state.token);
  settingsSheet.close();
  connect();
});

setInterval(refreshStatus, 2500);
connect();

if ('serviceWorker' in navigator && window.isSecureContext) {
  navigator.serviceWorker.register('/sw.js').catch(() => {});
}
