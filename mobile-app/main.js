const translations = {
  en: {
    appTitle: 'Robot Remote',
    notConnected: 'Not connected',
    setUp: 'Set up',
    cameraPaused: 'Camera paused',
    locked: 'Locked',
    offline: 'Offline',
    online: 'Online',
    checking: 'Checking',
    cameraOff: 'Camera off',
    cameraLive: 'Camera live',
    reconnecting: 'Reconnecting',
    connectingCamera: 'Connecting camera',
    standby: 'Standby',
    camera: 'Camera',
    stopCamera: 'Stop camera',
    emergencyStop: 'Emergency stop',
    drive: 'Drive',
    stopped: 'Stopped',
    forward: 'Forward',
    left: 'Left',
    stop: 'Stop',
    right: 'Right',
    back: 'Back',
    speed: 'Speed',
    moves: 'Moves',
    quickActions: 'Quick actions',
    sit: 'Sit',
    stretch: 'Stretch',
    wag: 'Wag',
    dance: 'Dance',
    light: 'Light',
    stand: 'Stand',
    mode: 'Mode',
    response: 'Response',
    signal: 'Signal',
    settings: 'Settings',
    authRequired: 'Authorization required',
    authorized: 'Authorized',
    denied: 'Access denied',
    gatewayNotConfigured: 'Gateway token missing',
    language: 'Language',
    robotOrGateway: 'Robot or gateway',
    accessCode: 'Access code',
    authNote: 'Only users with the gateway access code can send movement, camera, or emergency commands.',
    connect: 'Connect',
  },
  fr: {
    appTitle: 'Telecommande Robot',
    notConnected: 'Non connecte',
    setUp: 'Reglages',
    cameraPaused: 'Camera en pause',
    locked: 'Verrouille',
    offline: 'Hors ligne',
    online: 'En ligne',
    checking: 'Verification',
    cameraOff: 'Camera eteinte',
    cameraLive: 'Camera active',
    reconnecting: 'Reconnexion',
    connectingCamera: 'Connexion camera',
    standby: 'En attente',
    camera: 'Camera',
    stopCamera: 'Arreter camera',
    emergencyStop: 'Arret urgence',
    drive: 'Conduire',
    stopped: 'Arrete',
    forward: 'Avant',
    left: 'Gauche',
    stop: 'Stop',
    right: 'Droite',
    back: 'Arriere',
    speed: 'Vitesse',
    moves: 'Actions',
    quickActions: 'Actions rapides',
    sit: 'Assis',
    stretch: 'Etirer',
    wag: 'Remuer',
    dance: 'Danser',
    light: 'Lumiere',
    stand: 'Debout',
    mode: 'Mode',
    response: 'Reponse',
    signal: 'Signal',
    settings: 'Reglages',
    authRequired: 'Autorisation requise',
    authorized: 'Autorise',
    denied: 'Acces refuse',
    gatewayNotConfigured: 'Jeton passerelle manquant',
    language: 'Langue',
    robotOrGateway: 'Robot ou passerelle',
    accessCode: 'Code acces',
    authNote: 'Seuls les utilisateurs avec le code passerelle peuvent envoyer des commandes de mouvement, camera ou urgence.',
    connect: 'Connecter',
  },
  es: {
    appTitle: 'Control Robot',
    notConnected: 'Sin conexion',
    setUp: 'Ajustes',
    cameraPaused: 'Camara pausada',
    locked: 'Bloqueado',
    offline: 'Desconectado',
    online: 'En linea',
    checking: 'Verificando',
    cameraOff: 'Camara apagada',
    cameraLive: 'Camara activa',
    reconnecting: 'Reconectando',
    connectingCamera: 'Conectando camara',
    standby: 'Espera',
    camera: 'Camara',
    stopCamera: 'Detener camara',
    emergencyStop: 'Parada emergencia',
    drive: 'Conducir',
    stopped: 'Detenido',
    forward: 'Adelante',
    left: 'Izquierda',
    stop: 'Alto',
    right: 'Derecha',
    back: 'Atras',
    speed: 'Velocidad',
    moves: 'Movimientos',
    quickActions: 'Acciones rapidas',
    sit: 'Sentar',
    stretch: 'Estirar',
    wag: 'Mover',
    dance: 'Bailar',
    light: 'Luz',
    stand: 'Parar',
    mode: 'Modo',
    response: 'Respuesta',
    signal: 'Senal',
    settings: 'Ajustes',
    authRequired: 'Autorizacion requerida',
    authorized: 'Autorizado',
    denied: 'Acceso denegado',
    gatewayNotConfigured: 'Falta token de gateway',
    language: 'Idioma',
    robotOrGateway: 'Robot o gateway',
    accessCode: 'Codigo de acceso',
    authNote: 'Solo usuarios con el codigo del gateway pueden enviar comandos de movimiento, camara o emergencia.',
    connect: 'Conectar',
  },
};

const state = {
  target: localStorage.getItem('solar_mobile_target') || defaultTarget(),
  token: localStorage.getItem('solar_mobile_token') || '',
  language: localStorage.getItem('solar_mobile_language') || 'en',
  authenticated: false,
  live: false,
  moving: null,
  flash: false,
  heartbeatTimer: null,
  frameTimer: null,
  lastLatency: null,
  frameObjectUrl: null,
};

const cameraFeed = document.getElementById('camera-feed');
const feedEmpty = document.getElementById('feed-empty');
const connectionPill = document.getElementById('connection-pill');
const targetReadout = document.getElementById('target-readout');
const cameraState = document.getElementById('camera-state');
const heroMode = document.getElementById('hero-mode');
const driveState = document.getElementById('drive-state');
const liveButton = document.getElementById('live-button');
const estopBtn = document.getElementById('estop-button');
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
const languageSelect = document.getElementById('language-select');
const connectButton = document.getElementById('connect-button');
const authReadout = document.getElementById('auth-readout');
const flashButton = document.getElementById('flash-button');
const standButton = document.getElementById('stand-button');
const controlButtons = [
  liveButton,
  estopBtn,
  stopButton,
  speedSlider,
  flashButton,
  standButton,
  ...document.querySelectorAll('.drive-btn'),
  ...document.querySelectorAll('[data-mode]'),
];

targetInput.value = state.target;
tokenInput.value = state.token;
languageSelect.value = translations[state.language] ? state.language : 'en';
speedValue.textContent = Number(speedSlider.value).toFixed(1);

applyLanguage();
renderTarget();
setControlEnabled(false);
setAuthState('locked');

function defaultTarget() {
  if (location.protocol.startsWith('http')) return location.origin;
  return 'https://your-solar-gateway.example.com';
}

function t(key) {
  return translations[state.language]?.[key] || translations.en[key] || key;
}

function applyLanguage() {
  document.documentElement.lang = state.language;
  document.querySelectorAll('[data-i18n]').forEach((element) => {
    element.textContent = t(element.dataset.i18n);
  });
  liveButton.textContent = state.live ? t('stopCamera') : t('camera');
  speedValue.textContent = Number(speedSlider.value).toFixed(1);
  renderTarget();
  renderLatency();
}

function apiUrl(path, params = {}) {
  const base = /^https?:\/\//i.test(state.target) ? state.target.replace(/\/+$/, '') : `https://${state.target}`;
  const query = new URLSearchParams(params);
  return `${base}${path}${query.toString() ? `?${query}` : ''}`;
}

function authHeaders() {
  return state.token ? { 'x-solar-token': state.token } : {};
}

async function requestGateway(path, params = {}, options = {}) {
  const startedAt = performance.now();
  const response = await fetch(apiUrl(path, params), {
    ...options,
    headers: {
      ...authHeaders(),
      ...(options.headers || {}),
    },
  });
  state.lastLatency = Math.round(performance.now() - startedAt);
  renderLatency();
  return response;
}

async function requestRobot(path, params = {}, options = {}) {
  if (!state.authenticated && path !== '/auth/check') {
    throw new Error('not_authenticated');
  }
  const response = await requestGateway(path, params, options);
  if (response.status === 403 || response.status === 503) {
    state.authenticated = false;
    setControlEnabled(false);
    setLive(false);
    setAuthState('denied', response.status === 503 ? t('gatewayNotConfigured') : t('denied'));
    throw new Error('authorization_lost');
  }
  return response;
}

function renderLatency() {
  latencyReadout.textContent = state.lastLatency == null ? '--' : `${state.lastLatency} ms`;
}

function renderTarget() {
  targetReadout.textContent = state.target.replace(/^https?:\/\//i, '') || t('notConnected');
}

function setControlEnabled(enabled) {
  controlButtons.forEach((control) => {
    control.disabled = !enabled;
  });
}

function setAuthState(status, detail = '') {
  connectionPill.classList.toggle('online', status === 'online');
  connectionPill.classList.toggle('locked', status === 'locked' || status === 'denied');

  const labelByStatus = {
    checking: t('checking'),
    online: t('online'),
    offline: t('offline'),
    locked: t('locked'),
    denied: t('denied'),
  };

  connectionPill.innerHTML = `<span></span>${labelByStatus[status] || t('offline')}`;
  authReadout.textContent = detail || (status === 'online' ? t('authorized') : t('authRequired'));
  heroMode.textContent = status === 'online' ? t('standby') : labelByStatus[status] || t('locked');
}

async function connect() {
  setAuthState('checking');
  setControlEnabled(false);
  state.authenticated = false;
  setLive(false);
  clearDriveUi();

  try {
    const authResponse = await requestGateway('/auth/check', {}, { signal: AbortSignal.timeout(2500) });
    if (!authResponse.ok) {
      let detail = t('denied');
      try {
        const body = await authResponse.json();
        if (body.error === 'gateway_token_not_configured') detail = t('gatewayNotConfigured');
      } catch {}
      setAuthState('denied', detail);
      return;
    }

    state.authenticated = true;
    setControlEnabled(true);
    setAuthState('online', t('authorized'));
    await refreshStatus();
  } catch {
    setAuthState('offline');
  }
}

async function refreshStatus() {
  if (!state.authenticated) return;
  try {
    const response = await requestRobot('/status', {}, { signal: AbortSignal.timeout(2500) });
    if (!response.ok) throw new Error('status failed');
    const status = await response.json();
    const mode = String(status.mode || '--');
    modeReadout.textContent = translations[state.language]?.[mode] || mode;
    heroMode.textContent = mode === '--' ? t('standby') : mode;
    networkReadout.textContent = status.wifi_mode ? String(status.wifi_mode).replaceAll('_', ' ') : '--';
    setAuthState('online', t('authorized'));
  } catch {
    setAuthState('offline');
  }
}

async function requestFrame() {
  if (!state.live || !state.authenticated) return;

  try {
    const response = await requestRobot('/capture', { t: Date.now() }, { signal: AbortSignal.timeout(4000) });
    if (!response.ok) throw new Error('capture failed');
    const blob = await response.blob();
    const nextUrl = URL.createObjectURL(blob);
    const previousUrl = state.frameObjectUrl;
    state.frameObjectUrl = nextUrl;
    cameraFeed.src = nextUrl;
    if (previousUrl) URL.revokeObjectURL(previousUrl);
  } catch {
    cameraFeed.hidden = true;
    cameraState.textContent = t('reconnecting');
    scheduleFrame(600);
  }
}

function scheduleFrame(delay) {
  clearTimeout(state.frameTimer);
  if (state.live) state.frameTimer = setTimeout(requestFrame, delay);
}

cameraFeed.addEventListener('load', () => {
  cameraFeed.hidden = false;
  feedEmpty.hidden = true;
  cameraState.textContent = t('cameraLive');
  scheduleFrame(180);
});

function renderFeedEmpty(key) {
  feedEmpty.innerHTML = `<span class="feed-mark">SOLAR</span><strong>${t(key)}</strong>`;
}

function setLive(enabled) {
  state.live = enabled && state.authenticated;
  liveButton.textContent = state.live ? t('stopCamera') : t('camera');
  liveButton.classList.toggle('active', state.live);
  cameraState.textContent = state.live ? t('connectingCamera') : t('cameraOff');
  renderFeedEmpty(state.live ? 'connectingCamera' : 'cameraPaused');
  feedEmpty.hidden = false;
  clearTimeout(state.frameTimer);

  if (!state.live) {
    cameraFeed.hidden = true;
    cameraFeed.removeAttribute('src');
    if (state.frameObjectUrl) URL.revokeObjectURL(state.frameObjectUrl);
    state.frameObjectUrl = null;
    return;
  }

  requestFrame();
}

function movementFor(direction) {
  if (direction === 'forward') return { vx: 1, wz: 0 };
  if (direction === 'backward') return { vx: -1, wz: 0 };
  if (direction === 'left') return { vx: 0, wz: 1 };
  if (direction === 'right') return { vx: 0, wz: -1 };
  return { vx: 0, wz: 0 };
}

async function drive(direction) {
  if (!state.authenticated) return;
  state.moving = direction;
  driveState.textContent = t(direction === 'backward' ? 'back' : direction);
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
  }).catch(() => setAuthState('offline'));

  clearInterval(state.heartbeatTimer);
  state.heartbeatTimer = setInterval(() => {
    if (state.moving) requestRobot('/ping').catch(() => setAuthState('offline'));
  }, 450);
}

function clearDriveUi() {
  state.moving = null;
  clearInterval(state.heartbeatTimer);
  driveState.textContent = t('stopped');
  document.querySelectorAll('.drive-btn').forEach((button) => button.classList.remove('active'));
}

async function stopDrive() {
  if (!state.authenticated) return;
  clearDriveUi();
  await requestRobot('/cmd', { mode: 'stand', vx: 0, vy: 0, wz: 0 }).catch(() => setAuthState('offline'));
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

estopBtn.addEventListener('click', async () => {
  if (!state.authenticated) return;
  estopBtn.classList.add('active');
  await requestRobot('/estop').catch(() => setAuthState('offline'));
  clearDriveUi();
  modeReadout.textContent = 'E-stop';
  heroMode.textContent = 'E-stop';
});

speedSlider.addEventListener('input', () => {
  speedValue.textContent = Number(speedSlider.value).toFixed(1);
  if (state.moving) drive(state.moving);
});

document.querySelectorAll('[data-mode]').forEach((button) => {
  button.addEventListener('click', () => {
    requestRobot('/cmd', { mode: button.dataset.mode }).catch(() => setAuthState('offline'));
  });
});

flashButton.addEventListener('click', () => {
  state.flash = !state.flash;
  flashButton.classList.toggle('active', state.flash);
  requestRobot('/flash', { state: state.flash ? 1 : 0 }).catch(() => setAuthState('offline'));
});

standButton.addEventListener('click', stopDrive);

settingsButton.addEventListener('click', () => settingsSheet.showModal());

languageSelect.addEventListener('change', () => {
  state.language = languageSelect.value;
  localStorage.setItem('solar_mobile_language', state.language);
  applyLanguage();
  clearDriveUi();
  setAuthState(state.authenticated ? 'online' : 'locked', state.authenticated ? t('authorized') : t('authRequired'));
});

connectButton.addEventListener('click', (event) => {
  event.preventDefault();
  state.target = targetInput.value.trim() || defaultTarget();
  state.token = tokenInput.value.trim();
  state.language = languageSelect.value;
  localStorage.setItem('solar_mobile_target', state.target);
  localStorage.setItem('solar_mobile_token', state.token);
  localStorage.setItem('solar_mobile_language', state.language);
  estopBtn.classList.remove('active');
  applyLanguage();
  renderTarget();
  settingsSheet.close();
  connect();
});

setInterval(refreshStatus, 2500);

if ('serviceWorker' in navigator && window.isSecureContext) {
  navigator.serviceWorker.register('/sw.js').catch(() => {});
}

if (state.token) {
  connect();
} else {
  setTimeout(() => settingsSheet.showModal(), 250);
}
