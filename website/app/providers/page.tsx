// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowLeft } from "lucide-react";
import { ProviderDirectory } from "@/components/studio/ProviderDirectory";
import { PageSchema } from "@/components/studio/PageSchema";
import { withBasePath } from "@/lib/base-path";
import directory from "@/lib/provider-directory.json";
import { links, pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/providers/");

export default function ProvidersPage() {
  const modelCount = directory.providers.reduce((sum, provider) => sum + provider.models.length, 0);
  return <>
    <PageSchema route="/providers/" />
    <div className="container-site route-hero"><a className="route-back" href={withBasePath("/")}><ArrowLeft size={16} /> TaffyGo</a><h1>Find your AI provider.</h1><p>Claude, OpenAI, Gemini, DeepSeek, and more. Taffy works through the provider you connect, using your own key or an eligible plan. Requests go from your phone to that provider.</p><div className="catalog-summary"><div><strong>{directory.providers.length}</strong><span>provider entries</span></div><div><strong>{modelCount}</strong><span>model entries</span></div></div><p className="catalog-caveat">This is the browser’s included catalog, not a claim that every model has been tested on a phone. Unavailable entries are labelled. Access depends on your provider, region, key or plan. Some providers list additional models after connection.</p></div>
    <div className="container-site route-content"><ProviderDirectory /><section className="route-notes" aria-label="About the AI model catalog"><article><h2>Keys, plans, and costs</h2><p>TaffyGo is free. Provider usage may cost money, and an existing subscription may not include every model or connection method. Check the provider’s terms before connecting.</p></article><article><h2>About this catalog</h2><p>These entries come from {directory.version}, the same catalog included in the browser. A model can appear through more than one provider. Logos identify brands; they do not imply a partnership.</p><p><a href={`${links.repository}/blob/main/taffy-core/components/intelligence/core/rust/model-router/catalog/baseline.json`}>Read the catalog source</a> · <a href="https://svgl.app">Logos from SVGL</a></p></article></section></div>
  </>;
}
