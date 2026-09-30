// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { pageMetadata, routes } from "@/lib/site";
import robots from "@/app/robots";
// Build tooling is JavaScript and is exercised directly by this contract test.
// @ts-expect-error No separate declarations for build-only scripts.
import { pageToMarkdown } from "../scripts/export-markdown.mjs";

const read = (path: string) => readFileSync(new URL(`../public/${path}`, import.meta.url), "utf8");

describe("static Markdown exports", () => {
  it("preserves hidden FAQ and model details while excluding scripts and navigation", () => {
    const html = '<main><h1>Model directory</h1><nav>Navigation</nav><p>Choose <a href="/providers/">a provider</a>.</p><details><summary>Example provider</summary><ul><li>Model A</li><li>Model B</li></ul></details><script>secretScript()</script><button>Filter</button></main>';
    const output = pageToMarkdown(html, "https://taffygo.com/");
    expect(output).toContain("# Model directory");
    expect(output).toContain("[a provider](https://taffygo.com/providers/)");
    expect(output).toContain("### Example provider");
    expect(output).toContain("- Model A");
    expect(output).not.toMatch(/secretScript|Navigation|Filter/);
  });

  it("fails instead of publishing an empty Markdown page", () => {
    expect(() => pageToMarkdown('<main>Failure page</main>', 'https://taffygo.com/')).toThrow("No main heading");
  });

  it("publishes and advertises Markdown for every canonical page", () => {
    const index = JSON.parse(read("markdown-index.json"));
    expect(index.pages.map((page: { url: string }) => new URL(page.url).pathname)).toEqual(routes);
    for (const route of routes) {
      const path = route === "/" ? "index.md" : `${route.slice(1, -1)}.md`;
      const body = read(path);
      expect(body).toMatch(/^# /);
      expect(body).toContain(`Source: https://taffygo.com${route}`);
      expect(body.length).toBeGreaterThan(300);
      expect(pageMetadata(route).alternates?.types?.["text/markdown"]).toBe(`https://taffygo.com/${path}`);
    }
  });

  it("allows all crawlers and explicitly permits training on public first-party content", () => {
    expect(robots().rules).toEqual({ userAgent: "*", allow: "/" });
    const policy = JSON.parse(read(".well-known/ai-policy.json"));
    expect(policy.permissions).toEqual({ crawl: true, search: true, retrieval: true, summarization: true, training: true });
    expect(policy.excluded_first_party_public_paths).toEqual([]);
    expect(read("ai-policy.txt")).toContain("AI model training");
  });
});
