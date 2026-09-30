// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import directory from "@/lib/provider-directory.json";

const root = process.cwd();
const baseline = JSON.parse(readFileSync(resolve(root, "../taffy-core/components/intelligence/core/rust/model-router/catalog/baseline.json"), "utf8"));

describe("public catalog and asset integrity", () => {
  it("matches every enabled provider and every model, including availability", () => {
    expect(directory.version).toBe(baseline.catalog_version);
    expect(directory.providers.map(p => p.id)).toEqual(baseline.providers.filter((p: { enabled: boolean }) => p.enabled).map((p: { provider_id: string }) => p.provider_id));
    for (const provider of directory.providers) {
      const models = baseline.models.filter((m: { provider_id: string }) => m.provider_id === provider.id);
      expect(provider.models).toEqual(models.map((m: { model_id: string; display_name: string; input_modalities: string[]; reasoning: boolean; tool_calling: boolean; context_window: number; enabled: boolean }) => ({ id: m.model_id, name: m.display_name, vision: m.input_modalities.includes("IMAGE"), reasoning: m.reasoning, tools: m.tool_calling, context: m.context_window, enabled: m.enabled })));
    }
  });

  it("binds the served SVGL and studio assets to their source records", () => {
    const icons = JSON.parse(readFileSync(resolve(root, "public/providers/sources.json"), "utf8"));
    const art = JSON.parse(readFileSync(resolve(root, "design/asset-manifest.json"), "utf8"));
    for (const item of [...art, ...icons.map((icon: { id: string; sha256: string; source: string }) => ({ ...icon, path: `public/providers/${icon.id}.svg` }))]) {
      expect(createHash("sha256").update(readFileSync(resolve(root, item.path))).digest("hex"), item.path).toBe(item.sha256);
    }
  });
});
