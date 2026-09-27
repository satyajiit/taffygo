// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { createHash } from "node:crypto";
import { readFile, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";

const output = join(process.cwd(), "out");
const template = await readFile("lib/service-worker.js", "utf8");
const files = (await readdir(output, { recursive: true, withFileTypes: true }))
  .filter((entry) => entry.isFile())
  .map((entry) => join(entry.parentPath, entry.name).slice(output.length + 1))
  .filter((path) => path !== "sw.js")
  .sort();
const hash = createHash("sha256").update(template);
for (const path of files) hash.update(path).update(await readFile(join(output, path)));
const version = hash.digest("hex").slice(0, 16);
const base = (process.env.NEXT_PUBLIC_BASE_PATH ?? "").trim().replace(/^\/*|\/*$/g, "");
const home = await readFile(join(output, "index.html"), "utf8");
const homeAssets = [...home.matchAll(/\b(?:src|srcset)="([^"]+)"/gi)]
  .flatMap((match) => match[1].split(",").map((item) => item.trim().split(/\s+/)[0].replace(/^\//, "")))
  .map((path) => base && path.startsWith(`${base}/`) ? path.slice(base.length + 1) : path)
  .filter((path) => files.includes(path));
const precache = [...new Set(["./", "offline.html", ...homeAssets, ...files.filter((path) => path.startsWith("_next/static/"))])];
const worker = template.replace('"__TAFFY_VERSION__"', JSON.stringify(version))
  .replace("/* __TAFFY_PRECACHE__ */ []", JSON.stringify(precache));
await writeFile(join(output, "sw.js"), worker);
console.log(`Service worker ${version}: ${precache.length} precached files; automatic background updates.`);
