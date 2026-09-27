// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";
import { describe, expect, it, vi } from "vitest";

const source = readFileSync("lib/service-worker.js", "utf8")
  .replace('"__TAFFY_VERSION__"', '"test-build"')
  .replace("/* __TAFFY_PRECACHE__ */ []", '["./", "offline.html", "_next/static/app.js"]');

function worker(scope = "https://taffygo.com/taffygo/") {
  const handlers = new Map<string, (event: unknown) => void>();
  const cache = { addAll: vi.fn().mockResolvedValue(undefined), match: vi.fn(), put: vi.fn(), keys: vi.fn().mockResolvedValue([]), delete: vi.fn() };
  const caches = { open: vi.fn().mockResolvedValue(cache), keys: vi.fn().mockResolvedValue([]), delete: vi.fn().mockResolvedValue(true) };
  const self = { registration: { scope }, skipWaiting: vi.fn(), clients: { claim: vi.fn() }, addEventListener: (name: string, callback: (event: unknown) => void) => handlers.set(name, callback) };
  const fetch = vi.fn();
  runInNewContext(source, { self, caches, fetch, URL, Request, Set, Promise, AbortSignal });
  const lifetime = (name: string) => new Promise<void>((resolve, reject) => {
    handlers.get(name)!({ waitUntil: (work: Promise<void>) => work.then(resolve, reject) });
  });
  return { handlers, cache, caches, self, fetch, lifetime };
}

describe("service worker lifecycle", () => {
  it("finishes a fresh, base-path-aware precache before taking over", async () => {
    const app = worker();
    await app.lifetime("install");
    const requests = app.cache.addAll.mock.calls[0]![0] as Request[];
    expect(requests.map((request) => request.url)).toEqual([
      "https://taffygo.com/taffygo/", "https://taffygo.com/taffygo/offline.html", "https://taffygo.com/taffygo/_next/static/app.js",
    ]);
    expect(requests.every((request) => request.cache === "reload")).toBe(true);
    expect(app.self.skipWaiting).toHaveBeenCalledOnce();
  });

  it("keeps the old worker when a new precache fails", async () => {
    const app = worker();
    app.cache.addAll.mockRejectedValue(new Error("offline"));
    await expect(app.lifetime("install")).rejects.toThrow("offline");
    expect(app.self.skipWaiting).not.toHaveBeenCalled();
  });

  it("cleans only old caches within its own site scope", async () => {
    const app = worker();
    app.caches.keys.mockResolvedValue(["taffygo-site:/taffygo/:old:core", "taffygo-site:/other/:old:core", "unrelated", "taffygo-site:/taffygo/:test-build:core"]);
    await app.lifetime("activate");
    expect(app.caches.delete.mock.calls).toEqual([["taffygo-site:/taffygo/:old:core"]]);
    expect(app.self.clients.claim).toHaveBeenCalledOnce();
  });

  it("ignores external requests, submissions, other apps, and model discovery data", () => {
    const app = worker();
    for (const [url, method] of [
      ["https://api.example.com/photo.png", "GET"], ["https://taffygo.com/taffygo/form", "POST"],
      ["https://taffygo.com/other/logo.png", "GET"], ["https://taffygo.com/taffygo/providers.json", "GET"],
    ]) {
      const respondWith = vi.fn();
      app.handlers.get("fetch")!({ request: new Request(url!, { method }), respondWith });
      expect(respondWith).not.toHaveBeenCalled();
    }
  });
});
