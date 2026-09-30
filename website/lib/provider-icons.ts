// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

const icons: Record<string, string> = {
  anthropic: "anthropic", openai: "openai", "google-ai-studio": "google-ai-studio",
  deepseek: "deepseek", mistral: "mistral", groq: "groq", xai: "xai", qwen: "qwen",
  "qwen-token-plan": "qwen", "qwen-token-plan-cn": "qwen",
  "github-copilot": "github-copilot", huggingface: "huggingface", nvidia: "nvidia",
  together: "together", cerebras: "cerebras", moonshot: "moonshot", "moonshot-cn": "moonshot",
  "kimi-coding": "moonshot", openrouter: "openrouter", kilocode: "kilocode",
  opencode: "opencode", "opencode-go": "opencode", "vercel-ai-gateway": "vercel-ai-gateway",
};

export function providerIcon(id: string): string | undefined {
  return icons[id] ? `/providers/${icons[id]}.svg` : undefined;
}
