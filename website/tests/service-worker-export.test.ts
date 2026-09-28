// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { execFileSync } from "node:child_process";
import { describe, expect, it } from "vitest";

describe("offline export with a launch film", () => {
  it.each(["", "/taffygo"])("keeps large media out of the install cache at %s", (base) => {
    const fixture = mkdtempSync(join(tmpdir(), "taffygo-film-cache-"));
    try {
      mkdirSync(join(fixture, "out/media"), { recursive: true });
      mkdirSync(join(fixture, "lib"));
      writeFileSync(join(fixture, "lib/service-worker.js"), readFileSync("lib/service-worker.js"));
      for (const name of ["film.webm", "fallback.mp4", "voice.mp3", "poster.webp", "captions.vtt"]) {
        writeFileSync(join(fixture, "out/media", name), "fixture");
      }
      writeFileSync(join(fixture, "out/index.html"), `<video preload="none" poster="${base}/media/poster.webp"><source src="${base}/media/film.webm"><source src="${base}/media/fallback.mp4"><track src="${base}/media/captions.vtt"></video><audio src="${base}/media/voice.mp3"></audio>`);
      writeFileSync(join(fixture, "out/offline.html"), "Offline");
      execFileSync(process.execPath, [resolve("scripts/export-service-worker.mjs")], {
        cwd: fixture, env: { ...process.env, NEXT_PUBLIC_BASE_PATH: base },
      });
      const generated = readFileSync(join(fixture, "out/sw.js"), "utf8");
      const precache = JSON.parse(generated.match(/const PRECACHE = (\[.*\]);/)![1]!) as string[];
      expect(precache).toContain("media/poster.webp");
      expect(precache).toContain("media/captions.vtt");
      expect(precache).not.toContain("media/film.webm");
      expect(precache).not.toContain("media/fallback.mp4");
      expect(precache).not.toContain("media/voice.mp3");
    } finally {
      rmSync(fixture, { recursive: true, force: true });
    }
  });
});
