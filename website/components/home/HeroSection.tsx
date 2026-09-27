// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { hero } from "@/lib/content/home";
import { PhoneFrame } from "../PhoneFrame";
import { StoreLinks } from "../StoreLinks";

/**
 * The opening: the promise and the two ways to install on the left, and on
 * the right a strip of three real screens that runs off the edge of the
 * page. The root clips the overflow, so the strip never scrolls sideways.
 */
export function HeroSection() {
  return (
    <section aria-labelledby="hero-title" className="container-site hero">
      <div className="min-w-0">
        <h1 id="hero-title" className="type-display max-w-[14ch]">
          {hero.title}
        </h1>
        <p className="type-lead mt-6 max-w-[36rem] text-secondary">{hero.lede}</p>
        <StoreLinks className="mt-8" withSource />
        <ul aria-label={hero.factsLabel} className="fact-line mt-8 font-bold">
          {hero.facts.map((fact) => (
            <li key={fact}>{fact}</li>
          ))}
        </ul>
      </div>

      <div className="min-w-0" role="group" aria-label={hero.stripLabel}>
        <div className="hero-strip">
          {hero.strip.map((screen, index) => (
            <PhoneFrame
              key={screen}
              screen={screen}
              priority={index === 0}
              sizes="(min-width: 60rem) 17.5rem, 62vw"
            />
          ))}
        </div>
      </div>
    </section>
  );
}
