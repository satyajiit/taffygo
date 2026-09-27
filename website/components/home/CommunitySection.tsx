// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowUpRight, Bug, GitPullRequest, Lightbulb } from "lucide-react";
import { links } from "@/lib/site";

export function CommunitySection() {
  return <section className="container-site source-section community-section" aria-labelledby="source-title">
    <div>
      <span className="community-label"><GitPullRequest size={22} /> Made together</span>
      <h2 id="source-title">Built for the<br />open-source community.</h2>
      <p>TaffyGo’s Android interface, Chromium integration, Rust core, and Python runtime are on GitHub. Start with an issue, or contribute a fix.</p>
      <a className="text-link" href={links.repository}>Explore the code <ArrowUpRight size={17} /></a>
      <p className="community-license">Free to use. Source under the Mozilla Public License 2.0. Your AI provider’s charges are separate.</p>
    </div>
    <div className="community-invitation">
      <div className="contributor-thanks" aria-label="To our contributors: thank you for the bug reports, feature ideas, testing, and fixes.">
        <span>To our contributors</span>
        <p aria-hidden="true">Thank you for<br /><span className="community-roll"><span className="community-roll-track"><span>the bug reports.</span><span>the feature ideas.</span><span>testing on Android.</span><span>the fixes.</span><span>the bug reports.</span></span></span></p>
      </div>
      <div className="community-actions">
        <a className="btn btn-ink" href={links.featureRequest}><Lightbulb size={17} /> Request a feature <ArrowUpRight size={16} /></a>
        <a className="btn btn-line" href={links.bugReport}><Bug size={17} /> Report a bug <ArrowUpRight size={16} /></a>
      </div>
      <p className="community-note">Both open an issue form on GitHub.</p>
    </div>
  </section>;
}
