// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { siteHref } from "@/lib/base-path";
import type { TourShot } from "@/lib/content/home";
import { CalloutList, PhoneFrame } from "./PhoneFrame";

/**
 * One feature, shown on real screens. A single annotated screen sits beside
 * its heading with the pins explained in order; a pair of screens sits to
 * the right of the text, each with its own caption.
 */
export function ScreenStop({
  id,
  title,
  body,
  shots,
  link,
  headingLevel = 3,
}: {
  id: string;
  title: string;
  body: string;
  shots: ReadonlyArray<TourShot>;
  link?: { readonly label: string; readonly href: string };
  headingLevel?: 2 | 3;
}) {
  const Heading = headingLevel === 2 ? "h2" : "h3";
  const headingId = `${id}-title`;
  const single = shots.length === 1 ? shots[0] : undefined;

  const head = (
    <div className="stop-head min-w-0 max-w-[34rem]">
      <Heading id={headingId} className={headingLevel === 2 ? "type-h2" : "type-h3"}>
        {title}
      </Heading>
      <p className="mt-4 text-secondary">{body}</p>
    </div>
  );

  const more = link ? (
    <a href={siteHref(link.href)} className="link-arrow mt-4">
      {link.label}
    </a>
  ) : null;

  if (single) {
    return (
      <article id={id} aria-labelledby={headingId} className="stop stop-single scroll-mt-6">
        {head}
        <PhoneFrame
          className="stop-frame"
          screen={single.screen}
          callouts={single.callouts}
          caption={single.caption}
        />
        <div className="stop-notes min-w-0 max-w-[34rem]">
          {single.callouts ? <CalloutList callouts={single.callouts} /> : null}
          {more}
        </div>
      </article>
    );
  }

  return (
    <article id={id} aria-labelledby={headingId} className="stop stop-pair scroll-mt-6">
      <div className="min-w-0">
        {head}
        {more}
      </div>
      <div className="pair">
        {shots.map((shot) => (
          <PhoneFrame
            key={shot.screen}
            screen={shot.screen}
            caption={shot.caption}
            sizes="(min-width: 60rem) 20rem, 45vw"
          />
        ))}
      </div>
    </article>
  );
}
