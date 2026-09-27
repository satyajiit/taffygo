// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowUpRight, Star } from "lucide-react";
import { siteHref, withBasePath } from "@/lib/base-path";
import { footer } from "@/lib/content";
import { links } from "@/lib/site";

/** Product and publisher links share one compact footer. */
export function Footer() {
  return (
    <footer className="footer-dark studio-footer" data-taffy-theme="dark">
      <div className="container-site">
        <div className="footer-statement"><p>TaffyGo for Android</p><a href={links.repository} className="btn btn-outline"><Star size={18} /> Star on GitHub <ArrowUpRight size={18} /></a></div>
        <nav aria-label={footer.navLabel} className="footer-nav">{footer.links.map(link => <a key={link.href} href={siteHref(link.href)}>{link.label}</a>)}</nav>
        <div className="footer-community"><a href={links.featureRequest}>Request a feature <ArrowUpRight size={14} /></a><a href={links.bugReport}>Report a bug <ArrowUpRight size={14} /></a></div>
        <div className="machine-links"><a href={withBasePath("/llms.txt")}>For AI readers</a><a href={withBasePath("/index.md")}>Markdown</a><a href={withBasePath("/ai-policy.txt")}>AI training permission</a></div>
        <div className="footer-colophon">
          <a href="https://matterwardlabs.com" className="publisher-link"><span>Built by</span><img src={withBasePath("/brand/matterward-labs.svg")} width={240} height={60} alt="Matterward Labs" loading="lazy" /></a>
          <div><p>{footer.publisher}</p><p>{footer.chromium}</p><p>{footer.licence}</p><p>{footer.noTrackers} Brand icons from <a href="https://svgl.app">SVGL</a>.</p></div>
        </div>
      </div>
    </footer>
  );
}
