// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { links, routeMeta, routes } from "@/lib/site";

const root = fileURLToPath(new URL("..", import.meta.url));
const read = (path: string) => readFileSync(join(root, path), "utf8");

const llms = read("public/llms.txt");
const llmsFull = read("public/llms-full.txt");

const CANONICAL = /const CANONICAL_URL = "([^"]+)"/.exec(read("lib/site.ts"))?.[1];

const BANNED = [/early access/i, /\bbeta\b/i, /AI-powered/i, /wishlist/i];

// The retired pre-release wording. TaffyGo 1.0 is out, so none of it may
// survive in the files that describe the site to crawlers.
const PRE_RELEASE = [
  /not published yet/i,
  /not been released/i,
  /in development/i,
  /nothing to install/i,
  /before TaffyGo 1\.0 ships/i,
];

describe("llms.txt", () => {
  it("exists alongside a fuller version", () => {
    expect(existsSync(join(root, "public", "llms.txt"))).toBe(true);
    expect(existsSync(join(root, "public", "llms-full.txt"))).toBe(true);
  });

  it("pins the canonical origin used by the static files", () => {
    expect(CANONICAL).toBe("https://taffygo.com");
  });

  it("names the publisher and the public places to reach the project", () => {
    for (const text of [llms, llmsFull]) {
      expect(text).toContain("Matterward Labs Private Limited");
      expect(text).toContain(links.googlePlay);
      expect(text).toContain(links.releases);
      expect(text).toContain(links.repository);
      expect(text).not.toContain("@taffygo.com");
      expect(text).not.toContain(".example");
      expect(text).not.toContain("/privacy-policy/");
    }
    expect(llms).toContain(links.issues);
    expect(llms).toContain(links.discussions);
  });

  it("lists every route with its title and canonical URL", () => {
    for (const route of routes) {
      expect(llms).toContain(`](${CANONICAL}${route})`);
      expect(llms).toContain(`[${routeMeta[route].title}]`);
      expect(llmsFull).toContain(`${CANONICAL}${route}`);
    }
  });

  it("describes the released app, with no pre-release wording left", () => {
    for (const [name, text] of [
      ["llms.txt", llms],
      ["llms-full.txt", llmsFull],
    ] as const) {
      for (const pattern of [...BANNED, ...PRE_RELEASE]) {
        expect(pattern.test(text), `${name}: ${pattern}`).toBe(false);
      }
      expect(text).toMatch(/no account/i);
      expect(text).toMatch(/runs no server/i);
    }
  });

  it("keeps the fuller version small enough to stay useful", () => {
    expect(llmsFull.length).toBeLessThan(4096);
  });
});
