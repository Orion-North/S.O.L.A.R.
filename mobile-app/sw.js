const CACHE_NAME = 'solar-mobile-v3';
const APP_SHELL = ['/', '/index.html', '/style.css', '/main.js', '/manifest.webmanifest', '/icon.svg'];
const ROBOT_API_PREFIXES = [
  '/calib',
  '/capture',
  '/cmd',
  '/debug',
  '/estop',
  '/flash',
  '/imu',
  '/i2c',
  '/obs',
  '/ping',
  '/rl',
  '/seq',
  '/settings/',
  '/status',
  '/test',
  '/testseq',
  '/torque',
];

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(CACHE_NAME).then((cache) => cache.addAll(APP_SHELL)));
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) => Promise.all(keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key))))
  );
});

self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url);
  if (ROBOT_API_PREFIXES.some((path) => url.pathname.startsWith(path))) return;
  event.respondWith(caches.match(event.request).then((cached) => cached || fetch(event.request)));
});
