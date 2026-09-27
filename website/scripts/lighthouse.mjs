#!/usr/bin/env node
// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// website/scripts/lighthouse.mjs — the Lighthouse budget audit for the static
// export. Spec: docs/website/landing-page.md section 5 (mobile ≥ 95 in all
// four categories).
//
// Honesty rule: this script never reports a pass it did not measure. With no
// Chrome/Chromium on the host it prints a named SKIPPED verdict and exits 0,
// the same contract ./tools/check lanes follow. A missing export is a failure,
// not a skip: you asked for an audit of a build that does not exist.
//
// The local server mirrors the deploy target: GitHub Pages serves textual
// types gzip-compressed, and serves the export under the base path the build
// was made for (NEXT_PUBLIC_BASE_PATH, empty for taffygo.com and "/taffygo"
// for the project address). Lighthouse simulates mobile throughput from the
// observed transfer sizes, so the encoding matters; see COMPRESSIBLE below.
//
// Pages that only send the browser on to another address (the static
// redirect left at /privacy-policy/) are listed and skipped: they are noindex
// by design, and a score for a page nobody reads measures nothing.
//
// Exit codes: 0 = audited and within budget, or SKIPPED with a named reason;
// 1 = no export, Chrome failed to start, or a score is under budget.

import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync } from "node:fs";
import { access, mkdtemp, readFile, readdir, rm, stat } from "node:fs/promises";
import { tmpdir } from "node:os";
import { extname, join, normalize, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { gzipSync } from "node:zlib";

const WEBSITE_DIR = resolve(fileURLToPath(new URL("..", import.meta.url)));
const EXPORT_DIR = join(WEBSITE_DIR, "out");

// The same normalisation as lib/base-path.ts, kept in step by hand because
// this script runs under plain node and cannot import TypeScript.
const BASE_PATH = (() => {
  const trimmed = (process.env.NEXT_PUBLIC_BASE_PATH ?? "").trim().replace(/\/+$/, "");
  if (trimmed === "") return "";
  return trimmed.startsWith("/") ? trimmed : `/${trimmed}`;
})();

const BUDGET = 0.95;
const ACCESSIBILITY_TARGET = 1.0;
const CATEGORIES = ["performance", "accessibility", "best-practices", "seo"];

const MIME = {
  ".css": "text/css; charset=utf-8",
  ".html": "text/html; charset=utf-8",
  ".ico": "image/x-icon",
  ".jpg": "image/jpeg",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".txt": "text/plain; charset=utf-8",
  ".webmanifest": "application/manifest+json",
  ".webp": "image/webp",
  ".woff2": "font/woff2",
  ".xml": "application/xml; charset=utf-8",
};

// Textual types the production host (GitHub Pages) serves gzip-compressed
// when the client accepts it. The audit server does the same: Lighthouse
// simulates a slow-4G pipe from the observed transfer sizes, so serving raw
// bytes would audit an encoding no visitor receives, and serving brotli would
// flatter the site with one GitHub Pages does not use. Already-compressed
// media (png, jpg, webp, woff2, ico) goes as-is.
const COMPRESSIBLE = new Set([".css", ".html", ".js", ".json", ".svg", ".txt", ".webmanifest", ".xml"]);

function encodeBody(ext, body, acceptEncoding) {
  if (!COMPRESSIBLE.has(ext)) return { body, encoding: null };
  if (acceptEncoding.includes("gzip")) return { body: gzipSync(body), encoding: "gzip" };
  return { body, encoding: null };
}

function fail(message, remediation) {
  console.error(`FAIL: ${message}`);
  if (remediation) console.error(`      ${remediation}`);
  process.exit(1);
}

// Chrome discovery: an explicit environment override first, then the binary
// names the common packages install. No probing of versioned install paths —
// a host that hides its browser elsewhere can say so via CHROME_PATH.
async function findChrome() {
  const candidates = [process.env.CHROME_PATH, process.env.CHROME_BIN].filter(Boolean);
  const pathDirs = (process.env.PATH ?? "").split(":").filter(Boolean);
  for (const name of ["google-chrome", "google-chrome-stable", "chromium", "chromium-browser"]) {
    for (const dir of pathDirs) candidates.push(join(dir, name));
  }
  for (const candidate of candidates) {
    try {
      await access(candidate, 0o1); // executable by somebody
      return candidate;
    } catch {
      // keep looking
    }
  }
  return null;
}

// The export uses trailingSlash: every route is a directory with an
// index.html, which is also exactly how GitHub Pages serves it. A request
// outside the base path is a 404, as it is on the project address.
async function resolveExportFile(pathname) {
  let path = decodeURIComponent(pathname);
  if (BASE_PATH) {
    if (path !== BASE_PATH && !path.startsWith(`${BASE_PATH}/`)) return null;
    path = path.slice(BASE_PATH.length) || "/";
  }
  const rel = normalize(path).replace(/^([/\\])+/, "");
  if (rel.startsWith("..") || rel.includes(`..${sep}`)) return null;
  let file = join(EXPORT_DIR, rel);
  try {
    if ((await stat(file)).isDirectory()) file = join(file, "index.html");
    if (!existsSync(file)) return null;
    return file;
  } catch {
    return null;
  }
}

/** Discover the same trailing-slash routes GitHub Pages will serve. */
async function exportRoutes() {
  const found = [];

  async function visit(directory, relative) {
    const entries = await readdir(directory, { withFileTypes: true });
    // Next emits /404/ as an implementation detail of the static host. It is
    // intentionally not indexable, so it is not one of the product routes.
    if (
      relative !== "404" &&
      entries.some((entry) => entry.isFile() && entry.name === "index.html")
    ) {
      found.push(relative ? `/${relative.split(sep).join("/")}/` : "/");
    }
    for (const entry of entries) {
      if (!entry.isDirectory() || entry.name.startsWith("_") || entry.name.startsWith(".")) continue;
      await visit(join(directory, entry.name), join(relative, entry.name));
    }
  }

  await visit(EXPORT_DIR, "");
  return found.sort((left, right) => {
    if (left === "/") return -1;
    if (right === "/") return 1;
    return left.localeCompare(right);
  });
}

function serveExport() {
  return new Promise((resolveServer, reject) => {
    const server = createServer(async (req, res) => {
      const file = await resolveExportFile(new URL(req.url ?? "/", "http://local").pathname);
      if (!file) {
        res.writeHead(404, { "content-type": "text/plain" });
        res.end("not found");
        return;
      }
      const ext = extname(file);
      const { body, encoding } = encodeBody(
        ext,
        await readFile(file),
        req.headers["accept-encoding"] ?? "",
      );
      const headers = { "content-type": MIME[ext] ?? "application/octet-stream", vary: "accept-encoding" };
      if (encoding) headers["content-encoding"] = encoding;
      res.writeHead(200, headers);
      res.end(body);
    });
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => resolveServer(server));
  });
}

function waitForDevToolsPort(profileDir, chrome) {
  const portFile = join(profileDir, "DevToolsActivePort");
  const deadline = Date.now() + 15000;
  return new Promise((resolvePort, reject) => {
    const poll = setInterval(async () => {
      if (chrome.exitCode !== null) {
        clearInterval(poll);
        reject(new Error(`Chrome exited with code ${chrome.exitCode} before opening a debug port`));
        return;
      }
      try {
        const first = (await readFile(portFile, "utf8")).split("\n")[0].trim();
        clearInterval(poll);
        resolvePort(Number(first));
      } catch {
        if (Date.now() > deadline) {
          clearInterval(poll);
          reject(new Error("Chrome wrote no DevToolsActivePort file within 15s"));
        }
      }
    }, 100);
  });
}

async function main() {
  if (!existsSync(join(EXPORT_DIR, "index.html"))) {
    fail(
      `no static export at website/out/ — the audit measures the built site, not the dev server`,
      "Run ./tools/website build first, then ./tools/website audit.",
    );
  }

  const chromePath = await findChrome();
  if (!chromePath) {
    console.log("SKIPPED: no Chrome or Chromium executable found");
    console.log("         Looked at CHROME_PATH, CHROME_BIN, and PATH for");
    console.log("         google-chrome, google-chrome-stable, chromium, chromium-browser.");
    console.log("         Install one of them (or set CHROME_PATH) and re-run; this is a");
    console.log("         skip, not a pass — no scores were measured.");
    return;
  }

  const server = await serveExport();
  const port = server.address().port;
  const profileDir = await mkdtemp(join(tmpdir(), "taffy-lighthouse-"));
  const chrome = spawn(
    chromePath,
    [
      "--headless=new",
      "--disable-gpu",
      "--disable-dev-shm-usage",
      "--no-first-run",
      "--remote-debugging-port=0",
      `--user-data-dir=${profileDir}`,
      "about:blank",
    ],
    { stdio: "ignore" },
  );

  let failed = false;
  try {
    const debugPort = await waitForDevToolsPort(profileDir, chrome);
    const { default: lighthouse } = await import("lighthouse");
    const routePaths = [];
    const redirectStubs = [];
    for (const route of await exportRoutes()) {
      const html = await readFile(join(EXPORT_DIR, route, "index.html"), "utf8");
      (/http-equiv="refresh"/i.test(html) ? redirectStubs : routePaths).push(route);
    }
    const base = BASE_PATH || "(none)";
    console.log(
      `audit: ${routePaths.length} routes with ${chromePath} (mobile profile, base path ${base}, gzip)`,
    );
    for (const route of redirectStubs) {
      console.log(`  skipped ${route}: a static redirect page, noindex by design`);
    }

    for (const route of routePaths) {
      const url = `http://127.0.0.1:${port}${BASE_PATH}${route}`;
      console.log("");
      console.log(`  ${route}`);
      console.log("  category         score   budget  verdict");
      console.log("  ---------------  ------  ------  -------");
      const result = await lighthouse(url, {
        port: debugPort,
        formFactor: "mobile",
        screenEmulation: { mobile: true, disabled: false },
        logLevel: "error",
        output: "json",
      });
      if (!result) {
        console.log("  Lighthouse returned no result                         FAIL");
        failed = true;
        continue;
      }

      for (const name of CATEGORIES) {
        const score = result.lhr.categories[name]?.score;
        const target = name === "accessibility" ? ACCESSIBILITY_TARGET : BUDGET;
        if (score === null || score === undefined) {
          console.log(`  ${name.padEnd(15)}  (none)  ≥ ${target.toFixed(2)}  FAIL — not measured`);
          failed = true;
          continue;
        }
        const verdict = score >= target ? "ok" : "FAIL";
        if (verdict === "FAIL") failed = true;
        console.log(`  ${name.padEnd(15)}  ${score.toFixed(2)}   ≥ ${target.toFixed(2)}  ${verdict}`);
      }
    }
    console.log("");
  } finally {
    if (chrome.exitCode === null) {
      chrome.kill("SIGKILL");
      await new Promise((done) => chrome.once("exit", done));
    }
    server.close();
    await rm(profileDir, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
  }

  if (failed) {
    fail(
      "one or more Lighthouse scores are under budget",
      "The website contract requires mobile accessibility 1.00 and the other categories at least 0.95 on every route.",
    );
  }
  console.log("audit passed: every exported route is within budget");
}

main().catch((error) => fail(error instanceof Error ? error.message : String(error)));
