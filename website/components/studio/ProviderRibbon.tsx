// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowUpRight } from "lucide-react";
import { withBasePath } from "@/lib/base-path";
import directory from "@/lib/provider-directory.json";

const featured = [
  ["anthropic", "Claude"], ["openai", "OpenAI"], ["google-ai-studio", "Gemini"],
  ["deepseek", "DeepSeek"], ["mistral", "Mistral"], ["xai", "Grok"], ["openrouter", "OpenRouter"],
];

export function ProviderRibbon() {
  const models = directory.providers.reduce((sum, provider) => sum + provider.models.length, 0);
  return (
    <section className="container-site provider-ribbon" aria-label="Choose your AI provider">
      <div className="ribbon-heading"><p>Your browser. <strong>Your choice of AI.</strong></p><a href={withBasePath("/providers/")}>{directory.providers.length} providers · {models} model entries <ArrowUpRight size={15} /></a></div>
      <ul>{featured.map(([id, name]) => <li key={id}><a href={withBasePath("/providers/")}><span className="ribbon-logo"><img src={withBasePath(`/providers/${id}.svg`)} width={27} height={27} alt="" loading="lazy" /></span><span>{name}</span></a></li>)}</ul>
    </section>
  );
}
