// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { siteHref, withBasePath } from "@/lib/base-path";
import { header } from "@/lib/content";
import { ThemeSwitch } from "./ThemeSwitch";

/**
 * Edge-aligned bar: the lockup on the left edge, and on the right edge the
 * page links, the theme control and the one filled action. Below 48rem the
 * page links leave the bar; the footer lists every page, so nothing needs a
 * menu. The lockup swaps its light and dark artwork with the theme attribute.
 */
export function Header() {
  const lockup = (variant: "light" | "dark") => (
    <img
      src={withBasePath(`/brand/taffygo-lockup-color-on-${variant}-118.webp`)}
      srcSet={`${withBasePath(`/brand/taffygo-lockup-color-on-${variant}-118@2x.webp`)} 2x`}
      width={118}
      height={59}
      alt={header.lockupAlt}
      decoding="async"
      // Lazy, so the browser fetches only the lockup the theme shows: a
      // lazy image that is not displayed is never requested.
      loading="lazy"
      className={`lockup lockup-${variant}`}
    />
  );

  return (
    <header className="container-site flex h-[4.5rem] items-center justify-between gap-3">
      <a href={withBasePath("/")} aria-label={header.homeLabel} className="shrink-0">
        {lockup("light")}
        {lockup("dark")}
      </a>

      <div className="flex items-center gap-2 sm:gap-3">
        <nav aria-label={header.navLabel} className="hidden md:block">
          <ul className="flex items-center">
            {header.nav.map((item) => (
              <li key={item.href}>
                <a
                  href={siteHref(item.href)}
                  className="flex h-11 items-center px-3 whitespace-nowrap text-secondary transition-colors hover:text-primary"
                >
                  {item.label}
                </a>
              </li>
            ))}
          </ul>
        </nav>
        <ThemeSwitch />
        <a href={siteHref(header.getHref)} className="btn btn-ink btn-compact">
          <span className="sm:hidden">{header.getShortLabel}</span>
          <span className="hidden sm:inline">{header.getLabel}</span>
        </a>
      </div>
    </header>
  );
}
