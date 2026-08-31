/* ADDITION PWA — cache shell assets only. Never cache RPC or wallet responses. */
const CACHE = "addition-shell-v1";
const SHELL = [
  "/",
  "/common.css",
  "/chrome.js",
  "/common.js",
  "/explorer.js",
  "/local-rpc.js",
  "/manifest.webmanifest",
  "/logo-transparent.png",
  "/favicon.ico",
  "/apple-touch-icon.png",
  "/icon-192.png",
  "/icon-512.png",
  "/wallet/",
  "/join/",
  "/status/"
];

self.addEventListener("install", function (event) {
  event.waitUntil(
    caches.open(CACHE).then(function (cache) {
      return cache.addAll(SHELL);
    }).then(function () {
      return self.skipWaiting();
    }).catch(function () {
      return self.skipWaiting();
    })
  );
});

self.addEventListener("activate", function (event) {
  event.waitUntil(
    caches.keys().then(function (keys) {
      return Promise.all(keys.filter(function (k) {
        return k !== CACHE;
      }).map(function (k) {
        return caches.delete(k);
      }));
    }).then(function () {
      return self.clients.claim();
    })
  );
});

self.addEventListener("fetch", function (event) {
  const req = event.request;
  if (req.method !== "GET") {
    return;
  }
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) {
    return;
  }
  const path = url.pathname;
  if (
    path.indexOf("/api/") === 0 ||
    path.indexOf("/local-rpc") === 0 ||
    path === "/rpc" ||
    path.indexOf("/rpc/") === 0 ||
    path.indexOf("/jsonrpc") === 0
  ) {
    return;
  }
  event.respondWith(
    caches.match(req).then(function (cached) {
      const network = fetch(req).then(function (res) {
        if (res && res.ok && req.url.indexOf(self.location.origin) === 0) {
          const copy = res.clone();
          caches.open(CACHE).then(function (cache) {
            cache.put(req, copy);
          });
        }
        return res;
      }).catch(function () {
        return cached || caches.match("/");
      });
      return cached || network;
    })
  );
});
