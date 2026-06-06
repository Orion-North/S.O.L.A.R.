const { spawn } = require('node:child_process');
const { once } = require('node:events');

const port = Number(process.env.PORT || 5173);
const host = '127.0.0.1';
const url = `http://${host}:${port}`;
const npmCmd = process.platform === 'win32' ? 'npm.cmd' : 'npm';
const electronCmd = process.platform === 'win32'
  ? 'node_modules\\.bin\\electron.cmd'
  : 'node_modules/.bin/electron';

function spawnManaged(command, args, env = {}) {
  const child = spawn(command, args, {
    stdio: 'inherit',
    shell: false,
    env: { ...process.env, ...env },
  });
  child.on('exit', (code) => {
    if (!shuttingDown && code && code !== 0) process.exitCode = code;
  });
  return child;
}

async function waitForServer(targetUrl) {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(targetUrl);
      if (response.ok) return;
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 250));
    }
  }
  throw new Error(`Vite did not become ready at ${targetUrl}`);
}

let shuttingDown = false;
const vite = spawnManaged(npmCmd, ['run', 'dev', '--', '--host', host, '--port', String(port)]);

Promise.resolve()
  .then(() => waitForServer(url))
  .then(() => {
    const electron = spawnManaged(electronCmd, ['.'], { SOLAR_CONTROL_DEV_SERVER: url });
    return once(electron, 'exit');
  })
  .catch((error) => {
    console.error(error.message || error);
    process.exitCode = 1;
  })
  .finally(() => {
    shuttingDown = true;
    vite.kill();
  });
