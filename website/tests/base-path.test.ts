// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import { describe, expect, it } from "vitest";
import { normalizeBasePath, siteHref, withBasePath } from "@/lib/base-path";

const root = process.cwd();

function sources(directory: string): string[] {
  const out: string[] = [];
  const walk = (path: string) => {
    for (const entry of readdirSync(path)) {
      const full = join(path, entry);
      if (statSync(full).isDirectory()) walk(full);
      else if (/\.tsx?$/.test(entry)) out.push(full);
    }
  };
  walk(join(root, directory));
  return out;
}

describe("base path", () => {
  it("normalises the variable the Pages workflow sets", () => {
    expect(normalizeBasePath(undefined)).toBe("");
    expect(normalizeBasePath("")).toBe("");
    expect(normalizeBasePath("  ")).toBe("");
    expect(normalizeBasePath("/")).toBe("");
    expect(normalizeBasePath("/taffygo")).toBe("/taffygo");
    expect(normalizeBasePath("taffygo")).toBe("/taffygo");
    expect(normalizeBasePath("/taffygo/")).toBe("/taffygo");
  });

  it("is empty in the test run, so paths come back unchanged", () => {
    expect(withBasePath("/screens/01-start-page-360.webp")).toBe(
      "/screens/01-start-page-360.webp",
    );
    expect(siteHref("/privacy/")).toBe("/privacy/");
    expect(siteHref("https://github.com/satyajiit/taffygo")).toBe(
      "https://github.com/satyajiit/taffygo",
    );
    expect(siteHref("#collect")).toBe("#collect");
  });

  it("refuses a path it cannot prefix", () => {
    expect(() => withBasePath("screens/x.webp")).toThrow();
    expect(() => withBasePath("//cdn.example/x.webp")).toThrow();
  });

  it("leaves no root-absolute path in markup that skips the helper", () => {
    // A literal "/..." in src, srcSet or href would 404 on the project
    // address, where every path lives under /taffygo. Paths on this site go
    // through withBasePath or siteHref; next.config.ts reads the same
    // variable, so the two cannot disagree.
    const raw = /\b(src|srcSet|href)=(["'`]|\{["'`])\//;
    const problems: string[] = [];
    for (const file of [...sources("app"), ...sources("components")]) {
      const text = readFileSync(file, "utf8");
      text.split("\n").forEach((line, index) => {
        if (raw.test(line)) problems.push(`${relative(root, file)}:${index + 1}: ${line.trim()}`);
      });
    }
    expect(problems).toEqual([]);
  });

  it("gives next.config.ts the same base path as the markup", () => {
    const config = readFileSync(join(root, "next.config.ts"), "utf8");
    expect(config).toContain('from "./lib/base-path"');
    expect(config).toContain("basePath: BASE_PATH");
    expect(config).toContain("assetPrefix: BASE_PATH");
    expect(config).toContain('output: "export"');
    expect(config).toContain("trailingSlash: true");
  });
});
