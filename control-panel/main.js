// S.O.L.A.R. TACTICAL TERMINAL JavaScript - Refactored for ESP32 Gait Generation

document.addEventListener('DOMContentLoaded', () => {
  const ipInput = document.getElementById('ip-input');
  const tokenInput = document.getElementById('token-input');
  let apiToken = localStorage.getItem('solar_api_token') || '';
  if(tokenInput) tokenInput.value = apiToken;
  
  const navBtns = document.querySelectorAll('.nav-btn');
  navBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      navBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      const trg = btn.getAttribute('data-target');
      document.getElementById('view-tactical').style.display = trg === 'view-tactical' ? 'grid' : 'none';
      document.getElementById('view-calib').style.display = trg === 'view-calib' ? 'flex' : 'none';
      document.getElementById('view-path').style.display = trg === 'view-path' ? 'grid' : 'none';
      if(trg === 'view-calib') document.getElementById('view-calib').style.flexDirection = 'column';
    });
  });

  let tuneTimeout = null;
  async function liveTuneMotor(motorId, newVal, originalVal) {
    const target = ipInput.value.trim();
    if(!target) return;
    const spoofedAngle = 90 + newVal - originalVal;
    clearTimeout(tuneTimeout);
    tuneTimeout = setTimeout(async () => {
        try { await fetchRobot('/test', { motor: motorId, angle: spoofedAngle.toFixed(1) }); } catch(e) {}
    }, 40);
  }

  const applyBtn = document.getElementById('apply-ip');
  const camStream = document.getElementById('cam-stream');
  const pingBtn = document.getElementById('btn-ping');
  const flashBtn = document.getElementById('btn-flash');
  const liveFeedBtn = document.getElementById('btn-live-feed');
  const estopBtn = document.getElementById('btn-estop');
  const targetIpText = document.querySelector('.target-ip-text');
  const statusBlock = document.querySelector('.block');
  const terminalOut = document.getElementById('terminal-out');
  const telemetryOut = document.getElementById('telemetry-out');
  const sysTime = document.getElementById('sys-time');
  const calibContainer = document.getElementById('calib-container');
  const saveCalibBtn = document.getElementById('btn-save-calib');

  function buildRobotUrl(path, params = {}, targetOverride = null) {
    const target = (targetOverride || ipInput.value).trim();
    if(!target) return null;
    const base = /^https?:\/\//i.test(target) ? target.replace(/\/+$/, '') : `http://${target}`;
    const query = new URLSearchParams(params);
    if(apiToken) query.set('token', apiToken);
    const qs = query.toString();
    return `${base}${path}${qs ? `?${qs}` : ''}`;
  }

  async function fetchRobot(path, params = {}, options = {}) {
    const url = buildRobotUrl(path, params);
    if(!url) throw new Error('Missing target');
    return fetch(url, options);
  }

  function logTerminal(msg) {
    const time = new Date().toLocaleTimeString('en-US', {hour12:false});
    const row = document.createElement('div');
    row.textContent = `> [${time}] ${msg}`;
    terminalOut.appendChild(row);
    terminalOut.scrollTop = terminalOut.scrollHeight;
  }

  setInterval(() => {
    sysTime.innerText = new Date().toLocaleTimeString('en-US', {hour12:false});
  }, 1000);

  const savedIP = localStorage.getItem('solar_target_ip');
  if(savedIP) {
    ipInput.value = savedIP;
    updateTargets(savedIP);
  } else {
    logTerminal("AWAITING TARGET IP CONFIGURATION...");
  }

  async function updateTargets(ip) {
    logTerminal(`ESTABLISHING LINK TO ${ip}...`);
    targetIpText.innerText = `NODE: [SEARCHING]`;
    statusBlock.classList.add('offline');
    
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 2000);
      const res = await fetch(buildRobotUrl('/ping', {}, ip), { signal: controller.signal });
      clearTimeout(timeoutId);
      
      if(res.ok) {
        logTerminal("LINK ESTABLISHED. READY FOR CAPTURE...");
        targetIpText.innerText = `NODE: [${ip}]`;
        statusBlock.classList.remove('offline');
        loadCalibrationData(ip);
      } else {
        throw new Error('Bad Status');
      }
    } catch(e) {
      logTerminal(`ERROR: UNABLE TO REACH ${ip}. ARE YOU ON THE HOTSPOT?`);
      targetIpText.innerText = `NODE: [OFFLINE]`;
    }
  }

  applyBtn.addEventListener('click', () => {
    const ip = ipInput.value.trim();
    apiToken = tokenInput ? tokenInput.value.trim() : apiToken;
    localStorage.setItem('solar_api_token', apiToken);
    if(ip) {
      localStorage.setItem('solar_target_ip', ip);
      updateTargets(ip);
    }
  });

  pingBtn.addEventListener('click', async () => {
    const target = ipInput.value.trim();
    if (!target) return;

    logTerminal(`PINGING ${target}...`);
    pingBtn.innerText = '[TRANSMITTING...]';
    
    const start = Date.now();
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 2000);
      await fetchRobot('/ping', {}, { signal: controller.signal });
      clearTimeout(timeoutId);
      
      const latency = Date.now() - start;
      pingBtn.innerText = '[PING NODE]';
      logTerminal(`REPLY FROM ${target}: TIME=${latency}ms`);
    } catch (e) {
      pingBtn.innerText = '[PING NODE]';
      logTerminal(`REQUEST TIMED OUT FOR ${target}`);
    }
  });

  let flashState = false;
  flashBtn.addEventListener('click', async () => {
    const target = ipInput.value.trim();
    if (!target) return;

    flashState = !flashState;
    logTerminal(`TRANSMITTING FLASH OVERRIDE: ${flashState ? 'ON' : 'OFF'}...`);
    
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 2000);
      await fetchRobot('/flash', { state: flashState ? 1 : 0 }, { signal: controller.signal });
      clearTimeout(timeoutId);
      logTerminal(`FLASH COMMAND ACKNOWLEDGED.`);
    } catch (e) {
      logTerminal(`FLASH COMMAND FAILED.`);
    }
  });

  let liveFeed = false;
  let liveTimer = null;
  let liveErrorLogged = false;

  function setLiveFeedState(enabled) {
    liveFeed = enabled;
    clearTimeout(liveTimer);
    liveFeedBtn.innerText = enabled ? '[STOP FPV]' : '[FPV LIVE]';
    liveFeedBtn.style.backgroundColor = enabled ? 'rgba(0, 240, 255, 0.2)' : 'transparent';
    if(enabled) requestCameraFrame();
  }

  function requestCameraFrame() {
    const target = ipInput.value.trim();
    if(!target) return;
    camStream.src = buildRobotUrl('/capture', { t: Date.now() });
  }

  function scheduleLiveFrame(delayMs) {
    if(!liveFeed) return;
    clearTimeout(liveTimer);
    liveTimer = setTimeout(requestCameraFrame, delayMs);
  }

  camStream.addEventListener('load', () => {
    statusBlock.classList.remove('offline');
    const target = ipInput.value.trim();
    if(target) targetIpText.innerText = `NODE: [${target}]`;
    liveErrorLogged = false;
    scheduleLiveFrame(120);
  });

  camStream.addEventListener('error', async () => {
    if(liveFeed) {
      if(!liveErrorLogged) {
        logTerminal("FPV FRAME DROPPED. RETRYING...");
        liveErrorLogged = true;
      }
      scheduleLiveFrame(350);
      return;
    }
    statusBlock.classList.add('offline');
    targetIpText.innerText = `NODE: [OFFLINE]`;
    logTerminal("ERROR: SENSOR FEED LOST.");
  });

  // --- Telemetry Polling ---
  setInterval(async () => {
      const target = ipInput.value.trim();
      if (!target || statusBlock.classList.contains('offline')) return;
      try {
          const res = await fetchRobot('/status');
          if (res.ok) {
              const data = await res.json();
              telemetryOut.textContent = [
                `> UPTIME: ${(data.uptime_ms/1000).toFixed(1)}s`,
                `> MODE: ${String(data.mode || 'unknown').toUpperCase()}`,
                `> NET: ${String(data.wifi_mode || 'unknown').toUpperCase()} ${data.ip ? `(${data.ip})` : ''}`,
                `> GAIT: ${data.gait_hz} Hz`,
                `> CAM FPS LIMIT: ${data.camera_fps_limit}`,
                `> HEAP: ${(data.free_heap/1024).toFixed(1)} KB`,
                `> LAST CMD: ${data.last_cmd_ms_ago} ms`,
                `> TORQUE: ${data.torque_enabled ? 'ON' : 'OFF'}`,
                `> E-STOP: ${data.emergency_stop ? 'ACTIVE' : 'CLEAR'}`
              ].join('\n');
          }
      } catch (e) { }
  }, 1000);

  // --- Command Dispatcher ---
  async function dispatchCmd(params) {
      const target = ipInput.value.trim();
      if (!target) return;
      try {
          await fetchRobot('/cmd', params);
      } catch (e) { }
  }

  // Calibration Logic
  let currentSettings = {};
  const activeLegs = ["FL", "FR", "BL", "BR"];

  async function loadCalibrationData(ip) {
    try {
      const res = await fetch(buildRobotUrl('/settings/get', {}, ip));
      currentSettings = await res.json();
      if(!currentSettings.offsets) currentSettings.offsets = new Array(16).fill(0);
      renderCalibrationUI();
      logTerminal("CALIBRATION MATRIX SYNCED.");
    } catch(e) {
      logTerminal("FAILED TO SYNC CALIBRATION.");
    }
  }

  function renderCalibrationUI() {
    calibContainer.innerHTML = '';
    const legNames = { "FL": "FRONT LEFT", "FR": "FRONT RIGHT", "BL": "BACK LEFT", "BR": "BACK RIGHT" };

    activeLegs.forEach(leg => {
      let defaultSet = 1;
      if (leg === 'FR') defaultSet = 2;
      else if (leg === 'BR') defaultSet = 3;
      else if (leg === 'BL') defaultSet = 4;

      const setVal = currentSettings[`${leg}_SET`] || defaultSet;
      
      const row = document.createElement('div');
      row.className = 'calib-row';
      row.style.flexDirection = 'column';
      row.style.alignItems = 'flex-start';
      row.innerHTML = `
        <div style="display:flex; justify-content:space-between; width:100%;">
            <label style="font-weight: bold; color: var(--text-main); margin-bottom: 5px; display: block;">${legNames[leg]}</label>
            <div class="calib-controls">
            <span>SET:</span>
            <select id="set_${leg}" class="set-select" data-leg="${leg}">
                <option value="1" ${setVal == 1 ? 'selected' : ''}>SET 1 (0,1,2)</option>
                <option value="2" ${setVal == 2 ? 'selected' : ''}>SET 2 (3,4,5)</option>
                <option value="3" ${setVal == 3 ? 'selected' : ''}>SET 3 (6,7,8)</option>
                <option value="4" ${setVal == 4 ? 'selected' : ''}>SET 4 (9,10,11)</option>
            </select>
            </div>
        </div>
        <div id="offsets_${leg}" style="width:100%; padding-left:10px; border-left:1px dashed var(--text-muted); margin-top:5px;"></div>
      `;
      calibContainer.appendChild(row);

      const setSelect = row.querySelector(`#set_${leg}`);
      setSelect.addEventListener('change', (e) => {
        updateLegOffsets(leg, parseInt(e.target.value));
      });
      updateLegOffsets(leg, setVal);
    });
  }

  function updateLegOffsets(leg, setVal) {
    const setPins = [
      [0, 1, 2], [3, 4, 5], [6, 7, 8], [9, 10, 11]
    ];
    const pins = setPins[setVal - 1];
    const offCon = document.getElementById(`offsets_${leg}`);
    
    const getOff = (m) => currentSettings.offsets[m] || 0;

    offCon.innerHTML = '';
    ['HIP', 'KNEE', 'FOOT'].forEach((joint, idx) => {
      const motorId = pins[idx];
      const ctrl = document.createElement('div');
      ctrl.className = 'calib-controls';
      ctrl.style.marginTop = '5px';
      ctrl.style.justifyContent = 'space-between';
      ctrl.style.width = '100%';
      
      ctrl.innerHTML = `
        <span style="font-size: 0.8rem">${joint} (M${motorId}):</span>
        <div style="display:flex; gap:4px;">
            <input type="number" step="0.5" id="off_m${motorId}" value="${getOff(motorId)}" style="width:60px;" data-orig="${getOff(motorId)}" />
            <button class="btn btn-small test-btn" data-motor="${motorId}">TST</button>
        </div>
      `;
      
      const offInput = ctrl.querySelector('input');
      offInput.addEventListener('keydown', (e) => {
        if(e.key === 'ArrowUp' || e.key === 'ArrowDown') {
          e.preventDefault();
          const step = e.shiftKey ? 2.0 : 0.5;
          let val = parseFloat(offInput.value) || 0;
          val += (e.key === 'ArrowUp') ? step : -step;
          val = Math.round(val * 10) / 10;
          offInput.value = val;
          currentSettings.offsets[motorId] = val;
          liveTuneMotor(motorId, val, parseFloat(offInput.getAttribute('data-orig')));
        }
      });
      
      offInput.addEventListener('change', (e) => {
        const val = parseFloat(e.target.value) || 0;
        currentSettings.offsets[motorId] = val;
        liveTuneMotor(motorId, val, parseFloat(offInput.getAttribute('data-orig')));
      });
      
      ctrl.querySelector('.test-btn').addEventListener('click', () => {
        const target = ipInput.value.trim();
        if(target) fetchRobot('/test', { motor: motorId, angle: 90 });
      });
      
      offCon.appendChild(ctrl);
    });
  }

  saveCalibBtn.addEventListener('click', async () => {
    const target = ipInput.value.trim();
    if(!target) return;

    logTerminal("TRANSMITTING KINEMATICS TO NVS...");
    saveCalibBtn.innerText = '[SAVING...]';
    
    const params = {};
    activeLegs.forEach(leg => {
      const setVal = document.getElementById(`set_${leg}`).value;
      params[`${leg}_SET`] = setVal;
    });
    
    for(let i=0; i<16; i++) {
        if(currentSettings.offsets[i] !== undefined) {
            params[`o${i}`] = currentSettings.offsets[i];
        }
    }

    try {
      const res = await fetchRobot('/settings/set', params, { method: 'POST' });
      saveCalibBtn.innerText = '[SAVE TO NVS]';
      if(res.ok) {
          logTerminal("NVS WRITE CONFIRMED.");
          loadCalibrationData(target);
      }
    } catch(e) {
      saveCalibBtn.innerText = '[SAVE TO NVS]';
      logTerminal("NVS WRITE FAILED.");
    }
  });

  const torqueBtn = document.getElementById('btn-torque');
  let torqueState = true;
  function renderTorqueButton() {
    if(!torqueBtn) return;
    torqueBtn.innerText = torqueState ? '[DISABLE TORQUE]' : '[ENABLE TORQUE]';
    torqueBtn.style.color = torqueState ? 'var(--accent-amber)' : 'var(--text-main)';
    torqueBtn.style.borderColor = torqueState ? 'var(--accent-amber)' : 'var(--text-main)';
    torqueBtn.style.backgroundColor = torqueState ? 'transparent' : 'var(--accent-amber)';
  }
  if(torqueBtn) {
    torqueBtn.addEventListener('click', async () => {
      const target = ipInput.value.trim();
      if(!target) return;
      torqueState = !torqueState;
      logTerminal(`SERVO TORQUE: ${torqueState ? 'ENGAGED' : 'DISABLED'}`);
      renderTorqueButton();
      try { await fetchRobot('/torque', { state: torqueState ? 1 : 0 }); } catch(e) {}
    });
  }

  const calibModeBtn = document.getElementById('btn-calib-mode');
  let inCalibMode = false;
  if(calibModeBtn) {
    calibModeBtn.addEventListener('click', async () => {
      const target = ipInput.value.trim();
      if(!target) return;
      inCalibMode = !inCalibMode;
      logTerminal(`CALIBRATION MODE: ${inCalibMode ? 'ON' : 'OFF'}`);
      calibModeBtn.innerText = inCalibMode ? '[DISABLE CALIB MODE]' : '[ENABLE CALIB MODE]';
      calibModeBtn.style.color = inCalibMode ? 'var(--bg-color)' : 'var(--accent-red)';
      calibModeBtn.style.backgroundColor = inCalibMode ? 'var(--text-main)' : 'transparent';
      calibModeBtn.style.borderColor = inCalibMode ? 'var(--text-main)' : 'var(--accent-red)';
      try { await fetchRobot('/calib', { state: inCalibMode ? 1 : 0 }); } catch(e) {}
    });
  }

  const testSeqBtn = document.getElementById('btn-test-sequence');
  if(testSeqBtn) {
    testSeqBtn.addEventListener('click', async () => {
      const target = ipInput.value.trim();
      if(!target) return;
      logTerminal("STARTING LEG SET IDENTIFICATION...");
      try {
        await fetchRobot('/testseq');
        logTerminal("LEG SET IDENTIFICATION STARTED.");
      } catch(e) {
        logTerminal("LEG SET IDENTIFICATION FAILED.");
      }
    });
  }

  // --- Emotes (Now powered by ESP32) ---
  const emotes = {
      'btn-turtle': 'flip',
      'btn-dance': 'dance',
      'btn-sit': 'sit',
      'btn-stretch': 'stretch',
      'btn-wag': 'wag'
  };
  for (const [id, mode] of Object.entries(emotes)) {
      const btn = document.getElementById(id);
      if(btn) {
          btn.addEventListener('click', () => {
              logTerminal(`EMOTE: ${mode.toUpperCase()}`);
              dispatchCmd({ mode: mode });
          });
      }
  }

  const btnCapture = document.getElementById('btn-capture');
  if(btnCapture) {
      btnCapture.addEventListener('click', () => {
          const target = ipInput.value.trim();
          if(!target) return;
          logTerminal("CAPTURING SENSOR FRAME...");
          camStream.src = buildRobotUrl('/capture', { t: Date.now() });
      });
  }

  if(liveFeedBtn) {
      liveFeedBtn.addEventListener('click', () => {
          const target = ipInput.value.trim();
          if(!target) return;
          setLiveFeedState(!liveFeed);
          logTerminal(liveFeed ? "FPV LIVE FEED STARTED." : "FPV LIVE FEED STOPPED.");
      });
  }

  // --- WASD Kinematic Controls (ESP32 Mode) ---
  const keys = { w: document.getElementById('key-w'), a: document.getElementById('key-a'), s: document.getElementById('key-s'), d: document.getElementById('key-d') };
  const sliderPace = document.getElementById('slider-pace');
  const valPace = document.getElementById('val-pace');

  if(sliderPace) sliderPace.addEventListener('input', e => valPace.innerText = e.target.value);

  let activeDir = 'IDLE';

  async function triggerEmergencyStop() {
    Object.values(keys).forEach(el => el.classList.remove('active'));
    activeDir = 'IDLE';
    runningPath = false;
    torqueState = false;
    renderTorqueButton();
    logTerminal("EMERGENCY STOP TRANSMITTED.");
    try {
      await fetchRobot('/estop');
      logTerminal("E-STOP ACKNOWLEDGED. TORQUE DISABLED.");
    } catch(e) {
      logTerminal("E-STOP TRANSMISSION FAILED.");
    }
  }

  if(estopBtn) {
    estopBtn.addEventListener('click', triggerEmergencyStop);
  }

  function sendDirection(keyName) {
    const key = keyName.toLowerCase();
    if(!keys[key] || activeDir === key.toUpperCase()) return;
    Object.values(keys).forEach(el => el.classList.remove('active'));
    keys[key].classList.add('active');
    activeDir = key.toUpperCase();

    let vx = 0, wz = 0;
    if(activeDir === 'W') vx = 1;
    if(activeDir === 'S') vx = -1;
    if(activeDir === 'A') wz = 1;
    if(activeDir === 'D') wz = -1;

    const paceVal = parseInt(sliderPace ? sliderPace.value : 150);
    const speed = 150.0 / paceVal;
    dispatchCmd({ mode: 'walk', vx, vy: 0, wz, speed: speed.toFixed(2) });
  }

  function stopDirection(keyName = null) {
    const key = keyName ? keyName.toLowerCase() : null;
    if(key && keys[key]) keys[key].classList.remove('active');
    const pressed = Object.keys(keys).find(keyId => keys[keyId].classList.contains('active'));

    if(pressed) {
      sendDirection(pressed);
      return;
    }

    activeDir = 'IDLE';
    dispatchCmd({ mode: 'stand' });
  }

  Object.entries(keys).forEach(([keyName, el]) => {
    if(!el) return;
    el.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      el.setPointerCapture(e.pointerId);
      sendDirection(keyName);
    });
    el.addEventListener('pointerup', (e) => {
      e.preventDefault();
      stopDirection(keyName);
    });
    el.addEventListener('pointercancel', () => stopDirection(keyName));
  });

  document.addEventListener('keydown', (e) => {
    const activeEl = document.activeElement;
    if(activeEl && ['INPUT', 'SELECT', 'TEXTAREA'].includes(activeEl.tagName)) return;
    const k = e.key.toLowerCase();

    if (e.key === 'Escape' || e.key === ' ') {
      e.preventDefault();
      triggerEmergencyStop();
      return;
    }
    
    // Quick keys for emotes
    if (k === 'f') dispatchCmd({ mode: 'flip' });
    if (k === 'e') dispatchCmd({ mode: 'dance' });
    if (k === 'c') dispatchCmd({ mode: 'sit' });
    if (k === 'x') dispatchCmd({ mode: 'stretch' });
    if (k === 'z') dispatchCmd({ mode: 'wag' });
    if (k === 'q' && btnCapture) btnCapture.click();
    
    if(keys[k]) sendDirection(k);
  });

  document.addEventListener('keyup', (e) => {
    const k = e.key.toLowerCase();
    if(keys[k]) stopDirection(k);
  });

  // Keep alive heartbeat while moving
  setInterval(() => {
      if(activeDir !== 'IDLE') {
          // Send an empty ping to reset the ESP32 safety timeout
          const target = ipInput.value.trim();
          if(target) fetchRobot('/ping').catch(() => {});
      }
  }, 500);

  // Alive Idle Check (If doing nothing for 3 seconds, send idle mode)
  let idleTimer = null;
  function resetIdleTimer() {
      clearTimeout(idleTimer);
      idleTimer = setTimeout(() => {
          if(activeDir === 'IDLE' && !inCalibMode) {
              dispatchCmd({ mode: 'idle' });
          }
      }, 3000);
  }
  document.addEventListener('keydown', resetIdleTimer);
  document.addEventListener('click', resetIdleTimer);
  resetIdleTimer();

  // --- PATH PLANNER LOGIC ---
  const pathDurSlider = document.getElementById('path-duration-slider');
  const pathDurVal = document.getElementById('val-path-duration');
  if(pathDurSlider) pathDurSlider.addEventListener('input', e => pathDurVal.innerText = e.target.value);

  const pathQueue = [];
  const qContainer = document.getElementById('path-queue-container');

  function renderPathQueue() {
      if(!qContainer) return;
      qContainer.innerHTML = '';
      if(pathQueue.length === 0) {
          qContainer.innerHTML = '<div style="color:var(--text-muted); font-style:italic;">[QUEUE EMPTY]</div>';
          return;
      }
      pathQueue.forEach((item, index) => {
          const row = document.createElement('div');
          row.style.display = 'flex';
          row.style.justifyContent = 'space-between';
          row.style.padding = '8px';
          row.style.marginBottom = '5px';
          row.style.background = 'rgba(0, 255, 0, 0.05)';
          row.style.border = '1px solid var(--text-main)';
          row.style.color = 'var(--text-main)';
          row.style.fontFamily = 'monospace';
          row.innerHTML = `<span>${index+1}. ACT: [${item.action}]</span><span>DUR: ${item.duration}ms</span>`;
          qContainer.appendChild(row);
      });
  }

  document.querySelectorAll('.path-add-btn').forEach(btn => {
      btn.addEventListener('click', (e) => {
          const action = e.target.getAttribute('data-action');
          const dur = parseInt(pathDurSlider ? pathDurSlider.value : 1500);
          pathQueue.push({ action, duration: dur });
          renderPathQueue();
      });
  });

  const btnPathClear = document.getElementById('btn-path-clear');
  if(btnPathClear) btnPathClear.addEventListener('click', () => { pathQueue.length = 0; renderPathQueue(); });

  let runningPath = false;
  const btnPathRun = document.getElementById('btn-path-run');
  const btnPathStop = document.getElementById('btn-path-stop');

  if(btnPathRun) {
      btnPathRun.addEventListener('click', async () => {
          if(runningPath || pathQueue.length === 0) return;
          runningPath = true;
          logTerminal("INITIATING AUTONOMOUS PATH SEQUENCE LOOP...");
          btnPathRun.innerText = "[RUNNING...]";

          while(runningPath) {
              for(let i = 0; i < pathQueue.length; i++) {
                  if(!runningPath) break;
                  const item = pathQueue[i];
                  logTerminal(`PATH EXEC: ${item.action} for ${item.duration}ms`);
                  
                  if(item.action === 'F') {
                      dispatchCmd({ mode: 'flip' });
                      await new Promise(r => setTimeout(r, item.duration)); 
                  } else if (item.action === 'E') {
                      dispatchCmd({ mode: 'dance' });
                      await new Promise(r => setTimeout(r, item.duration)); 
                  } else if (item.action === 'WAIT') {
                      dispatchCmd({ mode: 'stand' });
                      await new Promise(r => setTimeout(r, item.duration));
                  } else {
                      let vx = 0, wz = 0;
                      if(item.action === 'W') vx = 1;
                      if(item.action === 'S') vx = -1;
                      if(item.action === 'A') wz = 1;
                      if(item.action === 'D') wz = -1;
                      const paceVal = parseInt(sliderPace ? sliderPace.value : 150);
                      const speed = 150.0 / paceVal; 
                      dispatchCmd({ mode: 'walk', vx, vy: 0, wz, speed: speed.toFixed(2) });
                      
                          // Heartbeat loop while waiting
                          const start = Date.now();
                          while(Date.now() - start < item.duration && runningPath) {
                              const target = ipInput.value.trim();
                          if(target) fetchRobot('/ping').catch(()=>{});
                          await new Promise(r => setTimeout(r, 500));
                      }
                  }
              }
          }
          dispatchCmd({ mode: 'stand' });
          btnPathRun.innerText = "[EXECUTE LOOP]";
          logTerminal("PATH SEQUENCE HALTED.");
      });
  }

  if(btnPathStop) {
      btnPathStop.addEventListener('click', () => {
          runningPath = false;
          dispatchCmd({ mode: 'stand' });
      });
  }
  renderPathQueue();

});
