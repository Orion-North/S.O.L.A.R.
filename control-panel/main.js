// S.O.L.A.R. TACTICAL TERMINAL JavaScript

document.addEventListener('DOMContentLoaded', () => {
  const ipInput = document.getElementById('ip-input');
  
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
        try { await fetch(`http://${target}/test?motor=${motorId}&angle=${spoofedAngle.toFixed(1)}`); } catch(e) {}
    }, 40);
  }
  const applyBtn = document.getElementById('apply-ip');
  const camStream = document.getElementById('cam-stream');
  const pingBtn = document.getElementById('btn-ping');
  const flashBtn = document.getElementById('btn-flash');
  const targetIpText = document.querySelector('.target-ip-text');
  const statusBlock = document.querySelector('.block');
  const terminalOut = document.getElementById('terminal-out');
  const sysTime = document.getElementById('sys-time');
  const calibContainer = document.getElementById('calib-container');
  const saveCalibBtn = document.getElementById('btn-save-calib');

  function logTerminal(msg) {
    const time = new Date().toLocaleTimeString('en-US', {hour12:false});
    terminalOut.innerHTML += `> [${time}] ${msg}<br>`;
    terminalOut.scrollTop = terminalOut.scrollHeight;
  }

  setInterval(() => {
    sysTime.innerText = new Date().toLocaleTimeString('en-US', {hour12:false});
  }, 1000);

  // Load from local storage if previously saved
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
      const res = await fetch(`http://${ip}/ping`, { signal: controller.signal });
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
      await fetch(`http://${target}/ping`, { signal: controller.signal });
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
      await fetch(`http://${target}/flash?state=${flashState ? 1 : 0}`, { signal: controller.signal });
      clearTimeout(timeoutId);
      logTerminal(`FLASH COMMAND ACKNOWLEDGED.`);
    } catch (e) {
      logTerminal(`FLASH COMMAND FAILED.`);
    }
  });

  // Handle explicit network breaks if the browser detects them
  camStream.addEventListener('error', async () => {
    statusBlock.classList.add('offline');
    targetIpText.innerText = `NODE: [OFFLINE]`;
    logTerminal("ERROR: SENSOR FEED LOST.");
    
    // Automatically fetch diagnostic logs from the ESP32
    try {
      const dbgRes = await fetch(`http://${ipInput.value.trim()}/debug`);
      const dbgText = await dbgRes.text();
      logTerminal("--- REMOTE DIAGNOSTICS ---");
      const lines = dbgText.split('\n').filter(l => l.trim().length > 0);
      lines.forEach(l => logTerminal(l));
      logTerminal("--------------------------");
    } catch(e) {
      logTerminal("DIAGNOSTICS UNAVAILABLE.");
    }
  });

  // Calibration Logic
  let currentSettings = {};
  const activeLegs = ["FL", "FR", "BL", "BR"];

  async function loadCalibrationData(ip) {
    try {
      const res = await fetch(`http://${ip}/settings/get`);
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
      [0, 1, 2],
      [3, 4, 5],
      [6, 7, 8],
      [9, 10, 11]
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
        testMotor(motorId, joint, leg);
      });
      
      offCon.appendChild(ctrl);
    });
  }

  async function testMotor(motorId, jointName, legName) {
    const target = ipInput.value.trim();
    if(!target) return;
    try {
      await fetch(`http://${target}/test?motor=${motorId}&angle=90`);
      logTerminal(`TEST COMMAND: ${legName} ${jointName} TO CENTER.`);
    } catch(e) {
      logTerminal(`TEST FAILED.`);
    }
  }

  saveCalibBtn.addEventListener('click', async () => {
    const target = ipInput.value.trim();
    if(!target) return;

    logTerminal("TRANSMITTING KINEMATICS TO NVS...");
    saveCalibBtn.innerText = '[SAVING...]';
    
    let url = `http://${target}/settings/set?`;
    activeLegs.forEach(leg => {
      const setVal = document.getElementById(`set_${leg}`).value;
      url += `${leg}_SET=${setVal}&`;
    });
    
    // In our map, we just overwrite the specific channel offsets 
    for(let i=0; i<16; i++) {
        if(currentSettings.offsets[i] !== undefined) {
            url += `o${i}=${currentSettings.offsets[i]}&`;
        }
    }

    try {
      const res = await fetch(url.slice(0, -1), { method: 'POST' });
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

  const testSeqBtn = document.getElementById('btn-test-sequence');
  if(testSeqBtn) {
    testSeqBtn.addEventListener('click', async () => {
      const target = ipInput.value.trim();
      if(!target) return;
      logTerminal("INITIATING IDENTIFICATION SEQUENCE...");
      try {
        await fetch(`http://${target}/testseq`);
      } catch(e) {
        logTerminal("FAILED TO TRIGGER SEQUENCE.");
      }
    });
  }

  const torqueBtn = document.getElementById('btn-torque');
  let torqueState = true;
  if(torqueBtn) {
    torqueBtn.addEventListener('click', async () => {
      const target = ipInput.value.trim();
      if(!target) return;
      torqueState = !torqueState;
      logTerminal(`SERVO TORQUE: ${torqueState ? 'ENGAGED' : 'DISABLED'}`);
      torqueBtn.innerText = torqueState ? '[DISABLE TORQUE]' : '[ENABLE TORQUE]';
      torqueBtn.style.color = torqueState ? 'var(--accent-amber)' : 'var(--text-main)';
      torqueBtn.style.borderColor = torqueState ? 'var(--accent-amber)' : 'var(--text-main)';
      torqueBtn.style.backgroundColor = torqueState ? 'transparent' : 'var(--accent-amber)';
      try {
        await fetch(`http://${target}/torque?state=${torqueState ? 1 : 0}`);
      } catch(e) {}
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
      try {
        await fetch(`http://${target}/calib?state=${inCalibMode ? 1 : 0}`);
      } catch(e) {}
    });
  }

  // --- WASD Kinematic Controls ---
  const keys = { w: document.getElementById('key-w'), a: document.getElementById('key-a'), s: document.getElementById('key-s'), d: document.getElementById('key-d') };
  
  const sliderStride = document.getElementById('slider-stride');
  const sliderLift = document.getElementById('slider-lift');
  const sliderPace = document.getElementById('slider-pace');
  const valStride = document.getElementById('val-stride');
  const valLift = document.getElementById('val-lift');
  const valPace = document.getElementById('val-pace');

  sliderStride.addEventListener('input', e => valStride.innerText = e.target.value);
  sliderLift.addEventListener('input', e => valLift.innerText = e.target.value);
  sliderPace.addEventListener('input', e => valPace.innerText = e.target.value);

  function getPhysics() {
    return {
      stride: parseInt(sliderStride.value),
      lift: parseInt(sliderLift.value),
      pace: parseInt(sliderPace.value)
    };
  }

  const SET_PINS = [ [0, 1, 2], [3, 4, 5], [6, 7, 8], [9, 10, 11] ];

  function getIdx(leg, joint) {
      const setVal = currentSettings[`${leg}_SET`] || (leg === 'FL' ? 1 : leg === 'FR' ? 2 : leg === 'BR' ? 3 : 4);
      const pins = SET_PINS[setVal - 1];
      if (joint === 'HIP') return pins[0];
      if (joint === 'KNEE') return pins[1];
      if (joint === 'FOOT') return pins[2];
  }

  function createFrame() {
      return new Array(16).fill(-1);
  }

  // --- DIRECT ANGULAR KINEMATICS (NO IK) ---
  // If any joint moves the wrong way, just change its 1 to -1 or vice versa!
  const SIGNS = {
      FL: { H: 1, K: 1, F: 1 },
      FR: { H: -1, K: -1, F: -1 }, // Assumes right side is physically mirrored
      BL: { H: 1, K: 1, F: 1 },
      BR: { H: -1, K: -1, F: -1 }
  };

  function mapAnglesToServos(fl_a, fr_a, bl_a, br_a) {
      let f = createFrame();
      const setA = (leg, a) => {
          if (!a) return;
          f[getIdx(leg, 'HIP')] = Math.max(0, Math.min(180, a.h));
          f[getIdx(leg, 'KNEE')] = Math.max(0, Math.min(180, a.k));
          f[getIdx(leg, 'FOOT')] = Math.max(0, Math.min(180, a.ft));
      };
      setA('FL', fl_a); setA('FR', fr_a); setA('BL', bl_a); setA('BR', br_a);
      return f;
  }

  function getLegPose(leg, swingAmp, isLifting) {
      const S = SIGNS[leg];
      const p = getPhysics();
      
      const K_LIFT = isLifting ? parseInt(p.lift) : 0;
      const F_LIFT = isLifting ? parseInt(p.lift) : 0; 
      
      return {
          h: 90 + (swingAmp * S.H),
          k: 90 + (K_LIFT * S.K),
          ft: 90 + (F_LIFT * S.F)
      };
  }

  function generateGait(dir) {
      const p = getPhysics();
      const ST_AMP = parseInt(p.stride) / 2; // Amplitude of the hip swing
      
      const FWD = ST_AMP;
      const BCK = -ST_AMP;

      let sy = { FL: 0, FR: 0, BL: 0, BR: 0 }; // Swing target
      let py = { FL: 0, FR: 0, BL: 0, BR: 0 }; // Plant target
      
      if (dir === 'W') {
          sy = { FL: FWD, FR: FWD, BL: FWD, BR: FWD };
          py = { FL: BCK, FR: BCK, BL: BCK, BR: BCK };
      } else if (dir === 'S') {
          sy = { FL: BCK, FR: BCK, BL: BCK, BR: BCK };
          py = { FL: FWD, FR: FWD, BL: FWD, BR: FWD };
      } else if (dir === 'A') { // Turn Left (Left legs reverse, Right legs forward)
          sy = { FL: BCK, FR: FWD, BL: BCK, BR: FWD };
          py = { FL: FWD, FR: BCK, BL: FWD, BR: BCK };
      } else if (dir === 'D') { // Turn Right
          sy = { FL: FWD, FR: BCK, BL: FWD, BR: BCK };
          py = { FL: BCK, FR: FWD, BL: BCK, BR: FWD };
      }

      // 2-Beat Diagonal Trot (Direct Angles)
      
      // Frame 1: Lift FL & BR while sweeping to target; push FR & BL back
      let f1 = mapAnglesToServos(
          getLegPose('FL', sy.FL, true), 
          getLegPose('FR', py.FR, false), 
          getLegPose('BL', py.BL, false), 
          getLegPose('BR', sy.BR, true)
      );

      // Frame 2: Plant FL & BR
      let f2 = mapAnglesToServos(
          getLegPose('FL', sy.FL, false), 
          getLegPose('FR', py.FR, false), 
          getLegPose('BL', py.BL, false), 
          getLegPose('BR', sy.BR, false)
      );

      // Frame 3: Lift FR & BL while sweeping to target; push FL & BR back
      let f3 = mapAnglesToServos(
          getLegPose('FL', py.FL, false), 
          getLegPose('FR', sy.FR, true), 
          getLegPose('BL', sy.BL, true), 
          getLegPose('BR', py.BR, false)
      );

      // Frame 4: Plant FR & BL
      let f4 = mapAnglesToServos(
          getLegPose('FL', py.FL, false), 
          getLegPose('FR', sy.FR, false), 
          getLegPose('BL', sy.BL, false), 
          getLegPose('BR', py.BR, false)
      );

      return [f1, f2, f3, f4];
  }

  let activeMovement = false;
  let activeDir = 'IDLE';

  async function processMovement() {
    if(activeMovement) return;
    activeMovement = true;

    while(activeDir !== 'IDLE') {
        const frames = generateGait(activeDir);
        const p = getPhysics();
        const target = ipInput.value.trim();

        for(const frame of frames) {
            if(activeDir === 'IDLE') break; 
            if(target) {
                const tStr = frame.map(v => v === -1 ? -1 : Math.round(v)).join(',');
                try {
                    await fetch(`http://${target}/seq?fast=1&t=${tStr}`);
                } catch(e) {}
            }
            await new Promise(resolve => setTimeout(resolve, p.pace));
        }
    }
    
    activeMovement = false;
  }

  async function runTurtleFlip() {
      if(activeMovement) return;
      activeMovement = true;
      const target = ipInput.value.trim();
      logTerminal("INITIATING CUSTOM LEVER FLIP!");

      const FL_H = getIdx('FL', 'HIP'), FL_K = getIdx('FL', 'KNEE'), FL_F = getIdx('FL', 'FOOT');
      const FR_H = getIdx('FR', 'HIP'), FR_K = getIdx('FR', 'KNEE'), FR_F = getIdx('FR', 'FOOT');
      const BL_H = getIdx('BL', 'HIP'), BL_K = getIdx('BL', 'KNEE'), BL_F = getIdx('BL', 'FOOT');
      const BR_H = getIdx('BR', 'HIP'), BR_K = getIdx('BR', 'KNEE'), BR_F = getIdx('BR', 'FOOT');

      const sendFrame = async (f, delayMs, fast) => {
          if(!target) return;
          const tStr = f.map(v => v === -1 ? -1 : Math.round(v)).join(',');
          try { await fetch(`http://${target}/seq?fast=${fast ? 1 : 0}&t=${tStr}`); } catch(e){}
          await new Promise(r => setTimeout(r, delayMs));
      };

      // Step 1: Position legs
      let f1 = createFrame();
      
      // Front hips straight (90 degrees facing forward)
      f1[FL_H] = 90;  
      f1[FR_H] = 90; 
      
      // Back hips inverted out of the way
      f1[BL_H] = 180; // Swept max back
      f1[BR_H] = 0;   // Swept max back

      // Front knees and feet straight (neutral 90)
      f1[FL_K] = 90;  
      f1[FR_K] = 90; 
      f1[FL_F] = 90;  
      f1[FR_F] = 90; 

      // Back knees and feet curled out of the way
      f1[BL_K] = 180; f1[BL_F] = 180;
      f1[BR_K] = 0;   f1[BR_F] = 0;

      await sendFrame(f1, 1500, false); // slow smooth transition

      // Step 2: Push up 90 degrees with the front knees to flip over
      let f2 = createFrame();
      f2[FL_H] = 90;  f2[FR_H] = 90; 
      f2[BL_H] = 180; f2[BR_H] = 0;
      f2[FL_F] = 90;  f2[FR_F] = 90;
      
      // Push 90 degrees 
      f2[FL_K] = 180; 
      f2[FR_K] = 0;  
      
      await sendFrame(f2, 1000, false); // powerful push

      // Step 3: Recover to neutral standing
      let f3 = createFrame();
      for(let i=0; i<16; i++) { f3[i] = 90; } 
      await sendFrame(f3, 1000, true);

      activeMovement = false;
  }

  const btnTurtle = document.getElementById('btn-turtle');
  if(btnTurtle) {
      btnTurtle.addEventListener('click', runTurtleFlip);
  }

  async function runDance() {
      if(activeMovement) return;
      activeMovement = true;
      const target = ipInput.value.trim();
      logTerminal("INITIATING DANCE SEQUENCE!");

      const sendFrame = async (f, delayMs, fast=true) => {
          if(!target) return;
          const tStr = f.map(v => v === -1 ? -1 : Math.round(v)).join(',');
          try { await fetch(`http://${target}/seq?fast=${fast ? 1 : 0}&t=${tStr}`); } catch(e){}
          await new Promise(r => setTimeout(r, delayMs));
      };

      // 1. Bob up and down 3 times using explicit direct angles based on SIGNS
      for(let i=0; i<3; i++) {
          let squat = createFrame();
          const squatAmt = 45;
          ['FL','FR','BL','BR'].forEach(leg => {
              squat[getIdx(leg, 'HIP')] = 90;
              squat[getIdx(leg, 'KNEE')] = Math.max(0, Math.min(180, 90 + squatAmt * SIGNS[leg].K));
              squat[getIdx(leg, 'FOOT')] = Math.max(0, Math.min(180, 90 + squatAmt * SIGNS[leg].F));
          });
          await sendFrame(squat, 250);

          let stand = createFrame();
          ['FL','FR','BL','BR'].forEach(leg => {
              stand[getIdx(leg, 'HIP')] = 90;
              stand[getIdx(leg, 'KNEE')] = 90;
              stand[getIdx(leg, 'FOOT')] = 90;
          });
          await sendFrame(stand, 250);
      }

      // 2. Shake side to side by alternating Hips
      for(let i=0; i<4; i++) {
          let leftShift = createFrame();
          ['FL','FR','BL','BR'].forEach(leg => {
              let legAlt = (leg === 'FL' || leg === 'BL') ? 1 : -1;
              leftShift[getIdx(leg, 'HIP')] = 90 + 35 * SIGNS[leg].H * legAlt;
              leftShift[getIdx(leg, 'KNEE')] = 90;
              leftShift[getIdx(leg, 'FOOT')] = 90;
          });
          await sendFrame(leftShift, 250);

          let rightShift = createFrame();
          ['FL','FR','BL','BR'].forEach(leg => {
              let legAlt = (leg === 'FL' || leg === 'BL') ? 1 : -1;
              rightShift[getIdx(leg, 'HIP')] = 90 - 35 * SIGNS[leg].H * legAlt; 
              rightShift[getIdx(leg, 'KNEE')] = 90;
              rightShift[getIdx(leg, 'FOOT')] = 90;
          });
          await sendFrame(rightShift, 250);
      }

      // Finish Back to Neutral Smoothly
      let standFinal = createFrame();
      ['FL','FR','BL','BR'].forEach(leg => {
          standFinal[getIdx(leg, 'HIP')] = 90;
          standFinal[getIdx(leg, 'KNEE')] = 90;
          standFinal[getIdx(leg, 'FOOT')] = 90;
      });
      await sendFrame(standFinal, 500, false);

      activeMovement = false;
  }

  const btnDance = document.getElementById('btn-dance');
  if(btnDance) {
      btnDance.addEventListener('click', runDance);
  }

  const btnCapture = document.getElementById('btn-capture');
  if(btnCapture) {
      btnCapture.addEventListener('click', () => {
          const target = ipInput.value.trim();
          if(!target) return;
          logTerminal("CAPTURING SENSOR FRAME...");
          camStream.src = `http://${target}/capture?t=${new Date().getTime()}`;
      });
  }

  document.addEventListener('keydown', (e) => {
    if (document.activeElement === ipInput) return;
    const k = e.key.toLowerCase();
    
    if (k === 'f') {
        runTurtleFlip();
        return;
    }
    if (k === 'e') {
        runDance();
        return;
    }
    if (k === 'q') {
        const btnCap = document.getElementById('btn-capture');
        if(btnCap) btnCap.click();
        return;
    }
    // Prevent multiple parallel sequences if multiple WASD exist
    if(keys[k] && activeDir !== k.toUpperCase()) {
      Object.values(keys).forEach(el => el.classList.remove('active'));
      keys[k].classList.add('active');
      activeDir = k.toUpperCase();
      processMovement();
    }
  });

  document.addEventListener('keyup', (e) => {
    const k = e.key.toLowerCase();
    if(keys[k]) {
      keys[k].classList.remove('active');
      const pressed = Object.keys(keys).find(key => keys[key].classList.contains('active'));
      activeDir = pressed ? pressed.toUpperCase() : 'IDLE';
    }
  });

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
          
          row.innerHTML = `
              <span>${index+1}. ACT: [${item.action}]</span>
              <span>DUR: ${item.duration}ms</span>
          `;
          qContainer.appendChild(row);
      });
  }

  document.querySelectorAll('.path-add-btn').forEach(btn => {
      btn.addEventListener('click', (e) => {
          const action = e.target.getAttribute('data-action');
          const dur = parseInt(pathDurSlider.value);
          pathQueue.push({ action, duration: dur });
          renderPathQueue();
      });
  });

  const btnPathClear = document.getElementById('btn-path-clear');
  if(btnPathClear) {
      btnPathClear.addEventListener('click', () => {
          pathQueue.length = 0;
          renderPathQueue();
      });
  }

  let runningPath = false;
  const btnPathRun = document.getElementById('btn-path-run');
  const btnPathStop = document.getElementById('btn-path-stop');

  if(btnPathRun) {
      btnPathRun.addEventListener('click', async () => {
          if(runningPath || pathQueue.length === 0) return;
          runningPath = true;
          logTerminal("INITIATING AUTONOMOUS PATH SEQUENCE LOOP...");
          btnPathRun.innerText = "[RUNNING...]";

          while(runningPath) { // Continuous loop until HALT
              for(let i = 0; i < pathQueue.length; i++) {
                  if(!runningPath) break;
                  const item = pathQueue[i];
                  logTerminal(`PATH EXEC: ${item.action} for ${item.duration}ms`);
                  
                  if(item.action === 'F') {
                      await runTurtleFlip();
                      await new Promise(r => setTimeout(r, 1000)); // wait for safety
                  } else if (item.action === 'E') {
                      await runDance();
                  } else if (item.action === 'WAIT') {
                      await new Promise(r => setTimeout(r, item.duration));
                  } else {
                      // WASD Direct Kinematics Loop
                      const start = Date.now();
                      const p = getPhysics();
                      const target = ipInput.value.trim();
                      while(Date.now() - start < item.duration && runningPath) {
                          const frames = generateGait(item.action);
                          for(const f of frames) {
                              if(!runningPath) break;
                              if(target) {
                                  const tStr = f.map(v => v === -1 ? -1 : Math.round(v)).join(',');
                                  try { await fetch(`http://${target}/seq?fast=1&t=${tStr}`); } catch(err){}
                              }
                              await new Promise(r => setTimeout(r, p.pace));
                          }
                      }
                  }
              }
          }
          btnPathRun.innerText = "[EXECUTE LOOP]";
          logTerminal("PATH SEQUENCE HALTED.");
      });
  }

  if(btnPathStop) {
      btnPathStop.addEventListener('click', () => {
          runningPath = false;
          activeMovement = false; // Halt running specific kinematics
          activeDir = 'IDLE';
      });
  }

  // Initial render empty
  renderPathQueue();

});
