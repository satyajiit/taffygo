// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { Metadata } from "next";
import { HeroSection } from "@/components/home/HeroSection";
import { Painting } from "@/components/PageHead";
import { Questions } from "@/components/home/Questions";
import { ScreenStop } from "@/components/ScreenStop";
import { StoreLinks } from "@/components/StoreLinks";
import { getTaffy } from "@/lib/content";
import { siteHref } from "@/lib/base-path";
import {
  download,
  facts,
  faqTitle,
  meetTaffy,
  stickyGet,
  tour,
  type Fact,
} from "@/lib/content/home";
import { pageMetadata } from "@/lib/site";

export const metadata: Metadata = pageMetadata("/");

/**
 * The home route: the promise with real screens beside it, a tour of what
 * the app does on annotated captures, Taffy in one painting, the facts in a
 * table, the questions people ask, and the two ways to install.
 */
export default function Home() {
  return (
    <>
      <HeroSection />

      <section id="tour" aria-labelledby="tour-title" className="tour scroll-mt-6">
        <div className="container-site pt-[var(--space-3xl)]">
          <div className="max-w-[42rem]">
            <h2 id="tour-title" className="type-h2">
              {tour.title}
            </h2>
            <p className="type-lead mt-4 text-secondary">{tour.lede}</p>
          </div>

          {tour.stops.map((stop) => (
            <ScreenStop key={stop.id} {...stop} />
          ))}

          <div className="sticky-get">
            <span className="hidden font-bold sm:inline">{stickyGet.label}</span>
            <a href={getTaffy.playHref}>{stickyGet.play}</a>
            <a href={getTaffy.apkHref}>{stickyGet.apk}</a>
          </div>
        </div>
      </section>

      <section
        aria-labelledby="meet-taffy-title"
        className="container-site band py-[var(--space-4xl)]"
      >
        <Painting
          name="website-hero"
          alt={meetTaffy.imageAlt}
          sizes="(min-width: 60rem) 60vw, 100vw"
        />
        <div className="min-w-0">
          <h2 id="meet-taffy-title" className="type-h2">
            {meetTaffy.title}
          </h2>
          <p className="mt-4 text-secondary">{meetTaffy.body}</p>
        </div>
      </section>

      <section
        id="facts"
        aria-labelledby="facts-title"
        className="container-site grid gap-8 pb-[var(--space-3xl)] lg:grid-cols-[minmax(0,1fr)_minmax(0,2fr)]"
      >
        <h2 id="facts-title" className="type-h2">
          {facts.title}
        </h2>
        <dl className="spec">
          {(facts.rows as ReadonlyArray<Fact>).map((row) => (
            <div key={row.label}>
              <dt>{row.label}</dt>
              <dd>
                {row.value}
                {row.link ? (
                  <>
                    <br />
                    <a
                      href={siteHref(row.link.href)}
                      className="font-bold underline decoration-1 underline-offset-4 hover:decoration-2"
                    >
                      {row.link.label}
                    </a>
                  </>
                ) : null}
              </dd>
            </div>
          ))}
        </dl>
      </section>

      <section
        id="questions"
        aria-labelledby="questions-title"
        className="container-site grid gap-8 pb-[var(--space-3xl)] lg:grid-cols-[minmax(0,1fr)_minmax(0,2fr)]"
      >
        <h2 id="questions-title" className="type-h2">
          {faqTitle}
        </h2>
        <Questions labelledBy="questions-title" />
      </section>

      <section id="get" aria-labelledby="get-title" className="tour scroll-mt-6">
        <div className="container-site py-[var(--space-3xl)]">
          <h2 id="get-title" className="type-display max-w-[12ch]">
            {download.title}
          </h2>
          <p className="type-lead mt-6 max-w-[36rem] text-secondary">{download.body}</p>
          <StoreLinks className="mt-8" withSource />
          <p className="mt-8 max-w-[36rem] text-secondary">{download.note}</p>
          <a href={download.bugHref} className="link-arrow mt-2">
            {download.bugLabel}
          </a>
        </div>
      </section>
    </>
  );
}
