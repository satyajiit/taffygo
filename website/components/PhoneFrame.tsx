// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { CSSProperties } from "react";
import { withBasePath } from "@/lib/base-path";
import type { Callout } from "@/lib/content/home";
import { SCREEN_SIZES, screens, type ScreenId } from "@/lib/content/screens";

type PinStyle = CSSProperties & { "--x": number; "--y": number };

/** The two served widths of one capture, as src and srcSet. */
export function screenSources(id: ScreenId) {
  const [small, large] = SCREEN_SIZES;
  const url = (width: number) => withBasePath(`/screens/${id}-${width}.webp`);
  return {
    src: url(small.width),
    srcSet: `${url(small.width)} ${small.width}w, ${url(large.width)} ${large.width}w`,
    width: small.width,
    height: small.height,
  };
}

/**
 * One real TaffyGo screen, captured on a phone with the status and
 * navigation bars cropped off. Nothing here draws a device: the frame is the
 * capture's own rounded edge and a hairline. Numbered pins mark what the
 * callout list beside the screen explains; they are hidden from assistive
 * technology because that list carries the same words.
 */
export function PhoneFrame({
  screen,
  sizes = "(min-width: 48rem) 20rem, 80vw",
  priority = false,
  callouts,
  caption,
  className = "",
}: {
  screen: ScreenId;
  /** The rendered width at each breakpoint, so the browser picks a file. */
  sizes?: string;
  /** Only the first screen a visitor sees is fetched eagerly. */
  priority?: boolean;
  callouts?: ReadonlyArray<Callout>;
  caption?: string;
  className?: string;
}) {
  const source = screenSources(screen);

  return (
    <figure className={`min-w-0 ${className}`}>
      <div className="phone-frame">
        <img
          src={source.src}
          srcSet={source.srcSet}
          sizes={sizes}
          width={source.width}
          height={source.height}
          alt={screens[screen].alt}
          decoding="async"
          loading={priority ? "eager" : "lazy"}
          fetchPriority={priority ? "high" : undefined}
        />
        {callouts && callouts.length > 0 ? (
          // The pins stay on the light projection: every annotated capture
          // is a light screen, whatever theme the page is in.
          <div aria-hidden="true" data-taffy-theme="light">
            {callouts.map((callout, index) => (
              <span
                key={callout.text}
                className="pin"
                style={{ "--x": callout.x, "--y": callout.y } as PinStyle}
              >
                {index + 1}
              </span>
            ))}
          </div>
        ) : null}
      </div>
      {caption ? (
        <figcaption className="type-small mt-3 text-secondary">{caption}</figcaption>
      ) : null}
    </figure>
  );
}

/** The numbered notes for a screen's pins, in pin order. */
export function CalloutList({ callouts }: { callouts: ReadonlyArray<Callout> }) {
  return (
    <ol className="callouts mt-6">
      {callouts.map((callout) => (
        <li key={callout.text} className="text-secondary">
          {callout.text}
        </li>
      ))}
    </ol>
  );
}
