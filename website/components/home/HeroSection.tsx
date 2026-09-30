// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { hero } from "@/lib/content/home";
import { ArrowDown, ArrowUpRight, Star } from "lucide-react";
import { withBasePath } from "@/lib/base-path";
import { links } from "@/lib/site";
import { ProductScene } from "../studio/ProductScene";
import { StoreLinks } from "../StoreLinks";

export function HeroSection() {
  return (
    <section aria-labelledby="hero-title" className="container-site studio-hero">
      <div className="hero-copy">
        <a className="open-source-note" href={links.repository}><span className="status-dot" /> Open source · Free for Android <ArrowUpRight size={14} /></a>
        <h1 id="hero-title">The AI-native<br /><span>browser for Android.</span></h1>
        <p className="hero-description">Open a tab. Ask a question. Hand Taffy a task. A complete Chromium browser with built-in ad blocking, on-device tools, and your choice of AI.</p>
        <StoreLinks className="hero-install" />
        <div className="hero-secondary"><a href={withBasePath("/#tour")}>Try a task <ArrowDown size={16} /></a><a href={links.repository}><Star size={16} /> Star on GitHub</a></div>
        <ul aria-label={hero.factsLabel} className="hero-facts">{hero.facts.map(fact => <li key={fact}>{fact}</li>)}</ul>
      </div>
      <ProductScene />
    </section>
  );
}
