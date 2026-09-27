// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { PageHead } from "@/components/PageHead";
import { ScreenStop } from "@/components/ScreenStop";
import { StoreLinks } from "@/components/StoreLinks";
import { phoneSections, phonesHero, phonesRequirements } from "@/lib/content/phones";
import { pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/built-for-phones/");

/** How TaffyGo fits a phone, each claim beside the screen that shows it. */
export default function BuiltForPhonesPage() {
  return (
    <>
      <PageHead id="phones-title" title={phonesHero.title} lede={phonesHero.lede} />

      <div className="tour">
        <div className="container-site pt-[var(--space-xl)] pb-[var(--space-2xl)]">
          {phoneSections.map((section) => (
            <ScreenStop key={section.id} {...section} headingLevel={2} />
          ))}
        </div>
      </div>

      <section aria-labelledby="requirements-title" className="container-site py-[var(--space-3xl)]">
        <h2 id="requirements-title" className="type-h2">
          {phonesRequirements.title}
        </h2>
        <p className="type-lead mt-4 max-w-[36rem] text-secondary">{phonesRequirements.body}</p>
        <StoreLinks className="mt-8" />
      </section>
    </>
  );
}
