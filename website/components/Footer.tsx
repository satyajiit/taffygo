// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { siteHref, withBasePath } from "@/lib/base-path";
import { footer, header } from "@/lib/content";

/**
 * Mast-headed footer on the app's dark surface in both themes: the lockup
 * and the line it stands for, one inline row with every page on the site,
 * then the small print. The legal and deletion pages live here rather than
 * in the header.
 */
export function Footer() {
  return (
    <footer className="footer-dark" data-taffy-theme="dark">
      <div className="container-site pt-[var(--space-3xl)] pb-[var(--space-xl)]">
        <img
          src={withBasePath("/brand/taffygo-lockup-color-on-dark-132.webp")}
          srcSet={`${withBasePath("/brand/taffygo-lockup-color-on-dark-132@2x.webp")} 2x`}
          width={132}
          height={66}
          alt={header.lockupAlt}
          decoding="async"
          loading="lazy"
        />
        <p className="type-h2 mt-6 max-w-[18ch]">{footer.tagline}</p>

        <nav aria-label={footer.navLabel} className="mt-[var(--space-2xl)] border-t border-outline pt-6">
          <ul className="flex flex-wrap gap-x-6 gap-y-1">
            {footer.links.map((link) => (
              <li key={link.href}>
                <a
                  href={siteHref(link.href)}
                  className="flex min-h-11 items-center whitespace-nowrap underline-offset-4 hover:underline"
                >
                  {link.label}
                </a>
              </li>
            ))}
          </ul>
        </nav>

        <div className="type-small mt-6 grid gap-1 text-secondary">
          <p>{footer.publisher}</p>
          <p>{footer.chromium}</p>
          <p>{footer.licence}</p>
          <p>{footer.noTrackers}</p>
        </div>
      </div>
    </footer>
  );
}
