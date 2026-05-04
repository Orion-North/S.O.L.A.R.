const CACHE_NAME = 'solar-mobile-v1';
const APP_SHELL = ['/', '/index.html', '/style.css', '/main.js', '/manifest.webmanifest', '/icon.svg'];

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
  if (['/capture', '/cmd', '/status', '/ping'].some((path) => url.pathname.startsWith(path))) return;
  event.respondWith(caches.match(event.request).then((cached) => cached || fetch(event.request)));
});
