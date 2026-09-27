// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { withBasePath } from "@/lib/base-path";

/** The paintings served under public/art/, each at two widths. */
const ART = {
  "website-hero": { widths: [800, 1600], height: 500 },
  "section-asks-first": { widths: [720, 1440], height: 540 },
  "section-on-your-phone": { widths: [720, 1440], height: 540 },
} as const;

export type ArtName = keyof typeof ART;

/** A painting of Taffy at the width the layout gives it. */
export function Painting({
  name,
  alt,
  sizes,
  priority = false,
}: {
  name: ArtName;
  alt: string;
  sizes: string;
  priority?: boolean;
}) {
  const { widths, height } = ART[name];
  const url = (width: number) => withBasePath(`/art/${name}-${width}.webp`);
  return (
    <div className="painting">
      <img
        src={url(widths[0])}
        srcSet={widths.map((width) => `${url(width)} ${width}w`).join(", ")}
        sizes={sizes}
        width={widths[0]}
        height={height}
        alt={alt}
        decoding="async"
        loading={priority ? "eager" : "lazy"}
      />
    </div>
  );
}

/**
 * The top of a product page: the route's one h1 and its lede, with an
 * optional painting beside them from 60rem.
 */
export function PageHead({
  id,
  title,
  lede,
  art,
}: {
  id: string;
  title: string;
  lede: string;
  art?: { name: ArtName; alt: string };
}) {
  return (
    <section
      aria-labelledby={id}
      className="container-site grid items-center gap-[var(--space-xl)] pt-[var(--space-xl)] pb-[var(--space-3xl)] lg:grid-cols-[minmax(0,6fr)_minmax(0,5fr)] lg:gap-[var(--space-3xl)]"
    >
      <div className="min-w-0">
        <h1 id={id} className="type-display max-w-[14ch]">
          {title}
        </h1>
        <p className="type-lead mt-6 max-w-[36rem] text-secondary">{lede}</p>
      </div>
      {art ? (
        <Painting
          name={art.name}
          alt={art.alt}
          sizes="(min-width: 60rem) 40vw, 100vw"
          priority
        />
      ) : null}
    </section>
  );
}
