/* NetSwitch service worker - NETWORK FIRST, the local copy when there is no network.
   With a network every page comes fresh from the server; without one the copy kept in this
   browser (local.html fills it) opens instead, so a demo never depends on the venue wifi. */
var CACHE = 'netswitch-v2.54.0';
var ASSETS = ['./', './index.html', './manifest.webmanifest', './icon-192.png', './icon-512.png'];

self.addEventListener('install', function (e) {
  e.waitUntil(caches.open(CACHE).then(function (c) { return c.addAll(ASSETS); }).then(function () { return self.skipWaiting(); }));
});

self.addEventListener('activate', function (e) {
  e.waitUntil(
    caches.keys().then(function (keys) {
      return Promise.all(keys.map(function (k) { if (k !== CACHE) return caches.delete(k); }));
    }).then(function () { return self.clients.claim(); })
  );
});

self.addEventListener('fetch', function (e) {
  var r = e.request;
  if (r.method !== 'GET') return;
  /* never touch the live measurements - a stored copy would report a fake (instant) speed or echo */
  if (r.cache === 'no-store' || r.cache === 'reload' || r.url.indexOf('speedtest.bin') >= 0 || r.url.indexOf('?cb=') >= 0) return;
  e.respondWith(
    fetch(r).then(function (res) {
      if (res && res.status === 200 && res.type === 'basic') {
        var copy = res.clone();
        caches.open(CACHE).then(function (c) { c.put(r, copy); });
      }
      return res;
    }).catch(function () {
      return caches.match(r, { ignoreSearch: true }).then(function (hit) { return hit || caches.match('./index.html'); });
    })
  );
});
