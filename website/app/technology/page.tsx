// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowLeft, ArrowUpRight } from "lucide-react";
import { EnginePresentation } from "@/components/studio/EnginePresentation";
import { PageSchema } from "@/components/studio/PageSchema";
import { withBasePath } from "@/lib/base-path";
import { links, pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/technology/");

export default function TechnologyPage() {
  return <>
    <PageSchema route="/technology/" />
    <div className="container-site route-hero"><a className="route-back" href={withBasePath("/")}><ArrowLeft size={16} /> TaffyGo</a><h1>Inside TaffyGo.</h1><p>A native Android interface, Chromium for web pages, Rust for tasks, and embedded Python for selected local tools. Explore how they fit together.</p></div>
    <EnginePresentation />
    <div className="container-site route-content"><section className="route-notes" aria-label="TaffyGo engineering details">
      <article><h2>Chromium</h2><p>Tabs, pages, navigation, and downloads rest on Chromium. You can use TaffyGo as a full manual browser without connecting an AI provider.</p></article>
      <article><h2>A sandboxed Rust core</h2><p>Portable task, policy, and model-routing logic lives in Rust. It runs as a sandboxed core service, separate from the browser process. The source makes those boundaries inspectable.</p></article>
      <article className="python-feature"><img src={withBasePath("/providers/python.svg")} width={40} height={40} alt="Python" /><h2>Python, built into the app</h2><p>The Android build includes CPython in a separate tool process. Registered document and spreadsheet builders accept structured data and return file bytes. No separate Python installation is needed.</p><p>The worker has limits for execution time, memory, and output. It runs registered tools; it does not accept arbitrary scripts from a page or a model. The worker compiles into the Android build, with phone-level execution and isolation checks still pending.</p></article>
      <article><h2>Built-in request blocking</h2><p>Built-in ad and tracker blocking stops matching requests. The filter engine checks requests before the browser loads them. Per-site controls let you turn blocking off when a page needs it.</p></article>
      <article><h2>Native Android controls</h2><p>Compose owns TaffyGo’s Android surfaces and trusted platform interactions. Back, forward, Ask Taffy, and tabs sit within reach at the bottom of your phone.</p></article>
      <article><h2>Direct provider connections</h2><p>Matterward Labs runs no server for TaffyGo. When you choose to use AI, the request and the page text it needs go to the provider you connected. Ordinary websites still receive your browsing requests.</p></article>
      <article><h2>Open-source code</h2><p>The first-party source is licensed under the Mozilla Public License 2.0. The TaffyGo name, logos, and character are reserved separately. Third-party components keep their own licences.</p><a className="text-link" href={links.repository}>Explore the source on GitHub <ArrowUpRight size={16} /></a></article>
    </section></div>
  </>;
}
