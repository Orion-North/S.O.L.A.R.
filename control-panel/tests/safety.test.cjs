const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { resolve } = require('node:path');
const { test } = require('node:test');
const vm = require('node:vm');

class Surface {
  constructor() {
    this.listeners = new Map();
    const classes = new Set();
    this.classList = {
      add: name => classes.add(name),
      remove: name => classes.delete(name),
      contains: name => classes.has(name),
    };
  }
  addEventListener(name, callback) {
    const callbacks = this.listeners.get(name) || [];
    callbacks.push(callback);
    this.listeners.set(name, callbacks);
  }
  emit(name, event = {}) {
    for (const callback of this.listeners.get(name) || []) callback(event);
  }
  setPointerCapture() {}
}

function controls() {
  const source = readFileSync(resolve(__dirname, '../main.js'), 'utf8');
  const elements = new Map(['w', 'a', 's', 'd'].map(key => [`key-${key}`, new Surface()]));
  const window = new Surface();
  const document = new Surface();
  document.hidden = false;
  document.hasFocus = () => !document.hidden;
  document.getElementById = id => elements.get(id) || null;
  const requests = [];
  const logs = [];
  const timers = [];
  let gamepad = null;
  let response = { ok: true, status: 200 };
  let pending = null;
  const context = vm.createContext({
    window, document, console, AbortSignal,
    navigator: { getGamepads: () => Array.isArray(gamepad) ? gamepad : [gamepad] },
    requestAnimationFrame: () => {},
    setInterval: callback => timers.push(callback),
    ipInput: { value: 'http://robot.invalid' },
    logTerminal: message => logs.push(message),
    fetchRobot: async (path, params) => {
      requests.push({ path, params });
      if (pending && path === '/cmd') await pending;
      return response;
    },
    renderTorqueButton: () => {},
    estopBtn: null, clearEstopBtn: null, btnCapture: null,
  });
  vm.runInContext('let estopLatched = false, torqueState = true;', context);
  vm.runInContext(source.slice(source.indexOf('  let commandQueue'), source.indexOf('  // Calibration Logic')), context);
  vm.runInContext('"use strict";\n' + source.slice(source.indexOf('  // --- WASD Kinematic Controls'), source.indexOf('  // Alive Idle Check')), context);
  vm.runInContext(`globalThis.api = {
    sendDirection, triggerEmergencyStop, pollGamepad, dispatchCmd, selectInput,
    flush: () => commandQueue,
    state: () => ({ activeDir, inputSuspended, inputMode }),
    clearEstop: () => { estopLatched = false; },
    setWorkspace: active => { stopLostInput(); operationsActive = active; },
    setRobotReady: ready => { if (!ready) stopLostInput(); robotReady = ready; }
  };`, context);
  return {
    api: context.api, window, document, elements, requests, logs, timers,
    setGamepad: value => { gamepad = value; },
    setResponse: value => { response = value; },
    holdRequests: promise => { pending = promise; },
  };
}

test('blur stops walking and stops heartbeats', async () => {
  const c = controls();
  c.api.sendDirection('w');
  await c.api.flush();
  c.window.emit('blur');
  await c.api.flush();
  assert.equal(c.api.state().activeDir, 'IDLE');
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  const count = c.requests.length;
  c.timers.forEach(callback => callback());
  assert.equal(c.requests.length, count);
  c.window.emit('focus');
  c.document.emit('keydown', { key: 'w', repeat: true });
  await c.api.flush();
  assert.equal(c.requests.length, count);
});

test('hiding the page and losing pointer capture stop movement', async () => {
  const c = controls();
  c.api.sendDirection('w');
  await c.api.flush();
  c.elements.get('key-w').emit('lostpointercapture');
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  c.api.sendDirection('a');
  await c.api.flush();
  c.document.hidden = true;
  c.document.emit('visibilitychange');
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  assert.equal(c.api.state().inputSuspended, true);
});

function pad(vx = 0) {
  return { id: 'test-pad', axes: [0, -vx, 0], buttons: Array.from({ length: 10 }, () => ({ pressed: false })) };
}

test('keyboard and controller forward/backward match the robot direction', async () => {
  const c = controls();
  c.api.sendDirection('w');
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.vx, -1);
  c.api.sendDirection('s');
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.vx, 1);
  c.api.selectInput('controller');
  c.setGamepad(pad());
  c.api.pollGamepad();
  c.setGamepad(pad(0.6));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.vx, '-1.00');
  c.setGamepad(pad(-0.6));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.vx, '1.00');
});

test('controller forward/backward is full strength with a neutral dead zone', async () => {
  const c = controls();
  c.api.selectInput('controller');
  c.setGamepad(pad());
  c.api.pollGamepad();
  c.setGamepad(pad(0.1));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.length, 0);
  c.setGamepad(pad(0.2));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.vx, '-1.00');
  const count = c.requests.length;
  c.setGamepad(pad(0.9));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.length, count);
  c.setGamepad(pad(-0.2));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.vx, '1.00');
  c.setGamepad(pad(-0.1));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
});

test('controller turning is full left or right outside the dead zone', async () => {
  const c = controls();
  const steer = (right, left = 0) => ({ ...pad(), axes: [left, 0, right] });
  c.api.selectInput('controller');
  c.setGamepad(steer(0));
  c.api.pollGamepad();
  c.setGamepad(steer(-0.2));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.wz, '1.00');
  const count = c.requests.length;
  c.setGamepad(steer(-0.9));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.length, count);
  c.setGamepad(steer(0.2));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.wz, '-1.00');
  c.setGamepad(steer(0.1));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  c.setGamepad(steer(0, -0.2));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.wz, '1.00');
});

test('gamepad disconnection stops walking and reconnect requires neutral', async () => {
  const c = controls();
  c.api.selectInput('controller');
  c.setGamepad(pad());
  c.window.emit('gamepadconnected', { gamepad: pad() });
  c.api.pollGamepad();
  c.setGamepad(pad(1));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'walk');
  c.window.emit('gamepaddisconnected', { gamepad: pad() });
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  const count = c.requests.length;
  c.window.emit('gamepadconnected', { gamepad: pad(1) });
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.length, count);
  c.api.selectInput('controller');
  c.setGamepad(pad());
  c.api.pollGamepad();
  c.setGamepad(pad(1));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'walk');
  c.setGamepad(null);
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
});

test('Bluetooth controller works outside slot zero and reconnects without an event', async () => {
  const c = controls();
  const bluetoothPad = vx => ({ ...pad(vx), id: 'DualSense Wireless Controller', index: 2, connected: true });
  c.api.selectInput('controller');
  c.setGamepad([null, null, bluetoothPad(0)]);
  c.api.pollGamepad();
  c.setGamepad([null, null, bluetoothPad(1)]);
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'walk');
  c.setGamepad([]);
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  const stoppedCount = c.requests.length;
  c.setGamepad([null, null, bluetoothPad(1)]);
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.length, stoppedCount);
  c.api.selectInput('controller');
  c.setGamepad([null, null, bluetoothPad(0)]);
  c.api.pollGamepad();
  c.setGamepad([null, null, bluetoothPad(1)]);
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'walk');
});

test('disconnecting another controller does not disable the selected DualSense', async () => {
  const c = controls();
  const dualSense = vx => ({ ...pad(vx), id: 'DualSense Wireless Controller', index: 1, connected: true });
  c.api.selectInput('controller');
  c.setGamepad([pad(), dualSense(0)]);
  c.api.pollGamepad();
  c.setGamepad([pad(), dualSense(1)]);
  c.api.pollGamepad();
  await c.api.flush();
  const count = c.requests.length;
  c.window.emit('gamepaddisconnected', { gamepad: { ...pad(), index: 0 } });
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.length, count);
  assert.equal(c.api.state().activeDir, 'GAMEPAD');
});

test('queued movement cannot overtake a stop after input loss', async () => {
  const c = controls();
  let release;
  c.holdRequests(new Promise(resolve => { release = resolve; }));
  c.api.sendDirection('w');
  await Promise.resolve();
  c.api.sendDirection('a');
  c.window.emit('blur');
  release();
  await c.api.flush();
  assert.deepEqual(c.requests.map(request => request.params.mode), ['walk', 'stand']);
});

test('rejected emergency stop is never logged as acknowledged', async () => {
  const c = controls();
  c.setResponse({ ok: false, status: 403 });
  await c.api.triggerEmergencyStop();
  assert.ok(c.logs.includes('E-STOP TX FAIL'));
  assert.ok(!c.logs.includes('E-STOP ACK | TORQUE OFF'));
});

test('emergency stop bypasses pending commands and cancels queued movement', async () => {
  const c = controls();
  let release;
  c.holdRequests(new Promise(resolve => { release = resolve; }));
  c.api.sendDirection('w');
  await Promise.resolve();
  c.api.sendDirection('a');
  await c.api.triggerEmergencyStop();
  assert.equal(c.requests.at(-1).path, '/estop');
  release();
  await c.api.flush();
  assert.deepEqual(c.requests.map(request => request.path), ['/cmd', '/estop']);
  assert.equal(c.api.state().activeDir, 'IDLE');
});

test('held gamepad input cannot restart walking after focus returns or E-stop clears', async () => {
  const c = controls();
  c.api.selectInput('controller');
  c.setGamepad(pad());
  c.window.emit('gamepadconnected', { gamepad: pad() });
  c.api.pollGamepad();
  c.setGamepad(pad(1));
  c.api.pollGamepad();
  await c.api.flush();
  c.window.emit('blur');
  await c.api.flush();
  c.window.emit('focus');
  let count = c.requests.length;
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.length, count);
  c.api.selectInput('controller');
  c.setGamepad(pad());
  c.api.pollGamepad();
  c.setGamepad(pad(1));
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'walk');
  await c.api.triggerEmergencyStop();
  c.api.clearEstop();
  count = c.requests.length;
  c.api.pollGamepad();
  await c.api.flush();
  assert.equal(c.requests.length, count);
});

test('desktop proxy forwards immediate stop while throttling movement', async () => {
  const handlers = new Map();
  const requests = [];
  const electron = {
    app: { whenReady: () => ({ then: () => {} }), on: () => {} },
    BrowserWindow: function () {},
    ipcMain: { handle: (name, callback) => handlers.set(name, callback) }, shell: {},
  };
  const context = vm.createContext({
    require: name => name === 'electron' ? electron : require(name),
    process, console, URL, Buffer, AbortController, setTimeout, clearTimeout,
    Date: { now: () => 10000 }, __dirname: resolve(__dirname, '../electron'),
    fetch: async url => {
      requests.push(String(url));
      return { ok: true, status: 200, headers: new Headers(), text: async () => 'CMD ACK' };
    },
  });
  vm.runInContext(readFileSync(resolve(__dirname, '../electron/main.cjs'), 'utf8'), context);
  const send = mode => handlers.get('robot-request')(null, {
    target: 'http://robot.invalid', path: '/cmd', params: { mode },
  });
  assert.equal((await send('walk')).status, 200);
  assert.equal((await send('walk')).status, 429);
  assert.equal((await send('stand')).status, 200);
  assert.equal(requests.length, 2);
  assert.ok(requests.at(-1).includes('mode=stand'));
});

test('switching to backup stops the controller and prevents competing input', async () => {
  const c = controls();
  c.api.selectInput('controller');
  c.setGamepad(pad()); c.api.pollGamepad();
  c.setGamepad(pad(1)); c.api.pollGamepad(); await c.api.flush();
  c.api.selectInput('backup'); await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  const count = c.requests.length;
  c.api.pollGamepad(); await c.api.flush();
  assert.equal(c.requests.length, count);
  c.api.sendDirection('a'); await c.api.flush();
  assert.equal(c.requests.at(-1).params.wz, 1);
});

test('controller selection blocks keyboard motion and ignores unrelated key releases', async () => {
  const c = controls(); c.api.selectInput('controller');
  c.api.sendDirection('w'); await c.api.flush();
  assert.equal(c.requests.length, 0);
  c.setGamepad(pad()); c.api.pollGamepad();
  c.setGamepad(pad(1)); c.api.pollGamepad(); await c.api.flush();
  const count = c.requests.length;
  c.document.emit('keyup', { key: 'w' }); await c.api.flush();
  assert.equal(c.requests.length, count);
});

test('calibration stops driving and blocks both input sources', async () => {
  const c = controls(); c.api.sendDirection('w'); await c.api.flush();
  c.api.setWorkspace(false); await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  const count = c.requests.length;
  c.api.sendDirection('a'); c.api.selectInput('controller');
  c.setGamepad(pad()); c.api.pollGamepad();
  c.setGamepad(pad(1)); c.api.pollGamepad(); await c.api.flush();
  assert.equal(c.requests.length, count);
});

test('Escape still emergency stops while editing a connection field', async () => {
  const c = controls(); c.document.activeElement = { tagName: 'INPUT' };
  c.document.emit('keydown', { key: 'Escape', preventDefault() {} });
  await new Promise(setImmediate);
  assert.equal(c.requests.at(-1).path, '/estop');
});

test('controller emergency trigger remains available when backup is selected', async () => {
  const c = controls(); const gp = pad(); gp.buttons[7].pressed = true;
  c.setGamepad(gp); c.api.pollGamepad(); await new Promise(setImmediate);
  assert.equal(c.requests.at(-1).path, '/estop');
});

test('backup keyboard events drive, release to stop, and leave text fields alone', async () => {
  const c = controls();
  c.document.emit('keydown', { key: 'w', preventDefault() {} });
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'walk');
  c.document.emit('keyup', { key: 'w' });
  await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
  const count = c.requests.length;
  c.document.activeElement = { tagName: 'INPUT' };
  c.document.emit('keydown', { key: 'w', preventDefault() {} });
  await c.api.flush();
  assert.equal(c.requests.length, count);
});

test('robot readiness blocks driving and requires neutral after motors or telemetry recover', async () => {
  const c = controls(); c.api.setRobotReady(false);
  c.api.sendDirection('w'); await c.api.flush();
  assert.equal(c.requests.length, 0);
  c.api.selectInput('controller'); c.setGamepad(pad(1)); c.api.pollGamepad();
  c.api.setRobotReady(true); c.api.pollGamepad(); await c.api.flush();
  assert.equal(c.requests.length, 0);
  c.setGamepad(pad()); c.api.pollGamepad();
  c.setGamepad(pad(1)); c.api.pollGamepad(); await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'walk');
  c.api.setRobotReady(false); await c.api.flush();
  assert.equal(c.requests.at(-1).params.mode, 'stand');
});
