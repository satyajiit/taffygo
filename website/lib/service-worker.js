// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Build-time placeholders are replaced by export-service-worker.mjs. */
const VERSION = "__TAFFY_VERSION__";
const PRECACHE = /* __TAFFY_PRECACHE__ */ [];
const SCOPE = new URL(self.registration.scope);
const PREFIX = `taffygo-site:${SCOPE.pathname}:`;
const CORE = `${PREFIX}${VERSION}:core`;
const RUNTIME = `${PREFIX}${VERSION}:runtime`;
const absolute = (path) => new URL(path, SCOPE).href;
const precached = new Set(PRECACHE.map(absolute));

self.addEventListener("install", (event) => {
  event.waitUntil((async () => {
    const cache = await caches.open(CORE);
    await cache.addAll(PRECACHE.map((path) => new Request(absolute(path), { cache: "reload" })));
    await self.skipWaiting();
  })());
});

self.addEventListener("activate", (event) => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.filter((name) => name.startsWith(PREFIX) && name !== CORE && name !== RUNTIME).map((name) => caches.delete(name)));
    await self.clients.claim();
  })());
});

async function remember(request, response) {
  if (!response.ok || response.type !== "basic") return;
  const cache = await caches.open(RUNTIME);
  await cache.put(request, response);
  const keys = await cache.keys();
  await Promise.all(keys.slice(0, Math.max(0, keys.length - 64)).map((key) => cache.delete(key)));
}

async function navigate(request, event) {
  try {
    const response = await fetch(request, { signal: AbortSignal.timeout(5000) });
    event.waitUntil(remember(request, response.clone()).catch(() => {}));
    return response;
  } catch {
    const runtime = await caches.open(RUNTIME);
    const core = await caches.open(CORE);
    return await runtime.match(request, { ignoreSearch: true })
      || await core.match(request, { ignoreSearch: true })
      || await core.match(absolute("offline.html"));
  }
}

async function staticAsset(request, event) {
  const core = await caches.open(CORE);
  const runtime = await caches.open(RUNTIME);
  const cached = await core.match(request) || await runtime.match(request);
  if (cached) return cached;
  const response = await fetch(request);
  event.waitUntil(remember(request, response.clone()).catch(() => {}));
  return response;
}

self.addEventListener("fetch", (event) => {
  const { request } = event;
  const url = new URL(request.url);
  if (request.method !== "GET" || url.origin !== SCOPE.origin || !url.pathname.startsWith(SCOPE.pathname)) return;
  if (request.destination === "video" || request.destination === "audio" || request.headers.has("range")) return;
  if (request.mode === "navigate") {
    event.respondWith(navigate(request, event));
  } else if (!url.search && (precached.has(url.href) || /\.(?:webp|png|svg|woff2)$/.test(url.pathname))) {
    event.respondWith(staticAsset(request, event));
  }
});
