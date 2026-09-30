// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowRight, PanelsTopLeft, Moon, Archive, KeyRound, Search, Download } from "lucide-react";
import { HeroSection } from "@/components/home/HeroSection";
import { LaunchFilm } from "@/components/home/LaunchFilm";
import { CommunitySection } from "@/components/home/CommunitySection";
import { Questions } from "@/components/home/Questions";
import { StoreLinks } from "@/components/StoreLinks";
import { ProviderRibbon } from "@/components/studio/ProviderRibbon";
import { WorkflowDemo } from "@/components/studio/WorkflowDemo";
import { BrowserWorkbench } from "@/components/studio/BrowserWorkbench";
import { PageAssistantPreview } from "@/components/studio/PageAssistantPreview";
import { LocalToolsPreview } from "@/components/studio/LocalToolsPreview";
import { ConnectionPreview } from "@/components/studio/ConnectionPreview";
import { EnginePresentation } from "@/components/studio/EnginePresentation";
import { withBasePath } from "@/lib/base-path";
import { faq, download } from "@/lib/content/home";
import { pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/");
const faqSchema = { "@context": "https://schema.org", "@type": "FAQPage", mainEntity: faq.map(item => ({ "@type": "Question", name: item.question, acceptedAnswer: { "@type": "Answer", text: item.answer } })) };

export default function Home() {
  return <>
    <script type="application/ld+json" dangerouslySetInnerHTML={{ __html: JSON.stringify(faqSchema).replace(/</g, "\\u003c") }} />
    <HeroSection />
    <LaunchFilm />
    <ProviderRibbon />
    <section className="container-site story-section" id="tour" aria-labelledby="story-title">
      <div className="section-intro"><h2 id="story-title">Watch Taffy<br /><span>work through a task.</span></h2><p>Try a document download or a banking form. Follow each step, complete the handover, and review the result.</p></div>
      <WorkflowDemo />
      <a className="text-link section-end-link" href={withBasePath("/use-cases/")}>Explore the use cases <ArrowRight size={18} /></a>
    </section>
    <BrowserWorkbench />
    <PageAssistantPreview />
    <section className="container-site feature-section" aria-labelledby="features-title">
      <h2 id="features-title" data-reveal>The browser features<br />you use every day.</h2>
      <div className="feature-list">
        {[
          { icon: PanelsTopLeft, title: "Separate task tabs", body: "Keep browsing in your own tabs while Taffy’s pages stay grouped in the tab switcher." },
          { icon: KeyRound, title: "Your provider connection", body: "Use your own API key or an eligible provider plan. Requests go directly from your phone to that provider." },
          { icon: Archive, title: "Encrypted backup files", body: "Create a backup file and keep it where you choose. Restore it from the app’s backup controls." },
          { icon: Moon, title: "Light and dark themes", body: "Follow Android’s appearance or choose a theme for TaffyGo in Settings." },
          { icon: Search, title: "Search, Ask, or Task", body: "Type in one address bar, then choose whether to search the web, ask a question, or start a task." },
          { icon: Download, title: "Downloads on your phone", body: "Save files from the web and find them in your downloads. No TaffyGo cloud account is required." },
        ].map(({ icon: Icon, title, body }) => <article key={title} data-reveal><Icon size={24} /><h3>{title}</h3><p>{body}</p></article>)}
      </div>
    </section>
    <LocalToolsPreview />
    <ConnectionPreview />
    <EnginePresentation />
    <CommunitySection />
    <section className="container-site studio-questions" aria-labelledby="questions-title"><h2 id="questions-title">Questions about TaffyGo</h2><Questions labelledBy="questions-title" /></section>
    <section id="get" className="download-section" aria-labelledby="get-title"><div className="container-site"><div className="download-top"><h2 id="get-title">Get TaffyGo<br /><span>for Android.</span></h2><div><p>Browse with Taffy.<br />Free and open source.</p><StoreLinks className="mt-6" withSource /><p className="download-note">Android 10 or later · 64-bit ARM</p></div></div><p className="download-note">{download.note}</p></div></section>
  </>;
}
