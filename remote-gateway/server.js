import { createReadStream, existsSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = resolve(fileURLToPath(new URL('.', import.meta.url)));
const repoRoot = resolve(__dirname, '..');

const port = Number(process.env.PORT || 8787);
const robotBaseUrl = (process.env.ROBOT_BASE_URL || 'http://solar.local').replace(/\/+$/, '');
const gatewayToken = process.env.GATEWAY_TOKEN || '';
const robotApiToken = process.env.ROBOT_API_TOKEN || '';
const staticRoot = resolve(process.env.MOBILE_APP_DIR || process.env.CONTROL_PANEL_DIR || join(repoRoot, 'mobile-app'));

const robotRoutes = new Set([
  '/ping',
  '/capture',
  '/debug',
  '/cmd',
  '/status',
  '/flash',
  '/flash/auto',
  '/estop',
  '/testseq',
  '/settings/get',
  '/settings/set',
  '/seq',
  '/torque',
  '/calib',
  '/test',
]);

const mimeTypes = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.webmanifest': 'application/manifest+json; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
};

function sendCors(res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'content-type,x-solar-token');
}

function isAuthorized(req, url) {
  if (!gatewayToken) return true;
  return url.searchParams.get('token') === gatewayToken || req.headers['x-solar-token'] === gatewayToken;
}

async function proxyRobot(req, res, url) {
  if (!isAuthorized(req, url)) {
    res.writeHead(403, { 'content-type': 'text/plain; charset=utf-8' });
    res.end('Forbidden');
    return;
  }

  const target = new URL(url.pathname + url.search, robotBaseUrl);
  if (gatewayToken) {
    target.searchParams.delete('token');
  }
  if (robotApiToken) {
    target.searchParams.set('token', robotApiToken);
  }

  try {
    const upstream = await fetch(target, {
      method: req.method,
      headers: { 'user-agent': 'solar-remote-gateway' },
    });

    sendCors(res);
    res.writeHead(upstream.status, {
      'content-type': upstream.headers.get('content-type') || 'application/octet-stream',
      'cache-control': 'no-store',
    });

    if (upstream.body) {
      for await (const chunk of upstream.body) res.write(chunk);
    }
    res.end();
  } catch (error) {
    res.writeHead(502, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'robot_unreachable', detail: String(error.message || error) }));
  }
}

function serveStatic(res, pathname) {
  const requested = pathname === '/' ? 'index.html' : pathname.replace(/^\/+/, '');
  const safePath = normalize(requested).replace(/^(\.\.[/\\])+/, '');
  let filePath = resolve(staticRoot, safePath);
  const publicPath = resolve(staticRoot, 'public', safePath);

  if ((!existsSync(filePath) || !statSync(filePath).isFile()) && existsSync(publicPath) && statSync(publicPath).isFile()) {
    filePath = publicPath;
  }

  if (!filePath.startsWith(staticRoot) || !existsSync(filePath) || !statSync(filePath).isFile()) {
    res.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' });
    res.end('Not found');
    return;
  }

  const contentType = mimeTypes[extname(filePath).toLowerCase()] || 'application/octet-stream';
  res.writeHead(200, { 'content-type': contentType });
  createReadStream(filePath).pipe(res);
}

createServer(async (req, res) => {
  const url = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);
  sendCors(res);

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  if (robotRoutes.has(url.pathname)) {
    await proxyRobot(req, res, url);
    return;
  }

  serveStatic(res, url.pathname);
}).listen(port, () => {
  console.log(`SOLAR remote gateway listening on http://localhost:${port}`);
  console.log(`Forwarding robot API to ${robotBaseUrl}`);
});
