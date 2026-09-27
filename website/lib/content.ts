// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/**
 * Site-wide shared copy: the header, the footer and the ways to get the app.
 * Copy that belongs to one route lives in `lib/content/` next to this file
 * (home.ts for "/", one module per standing route) so it can be reviewed and
 * tested in one place per page.
 *
 * Content authority: docs/website/landing-page.md; vocabulary authority:
 * docs/voice-and-naming.md. No claim may go beyond what the app does in the
 * screens under public/screens/ and what docs/security/data-and-privacy.md
 * states.
 */

import { links } from "./site";

export const header = {
  homeLabel: "TaffyGo home",
  lockupAlt: "TaffyGo",
  navLabel: "Main",
  /** Shown from 48rem up; the footer lists every page at every width. */
  nav: [
    { label: "Errands", href: "/product/" },
    { label: "Built for phones", href: "/built-for-phones/" },
    { label: "Privacy", href: "/privacy/" },
  ],
  /** The header's one action: the download section on the home page. */
  getLabel: "Get TaffyGo",
  getShortLabel: "Get it",
  getHref: "/#get",
} as const;

/**
 * The three ways to get or read TaffyGo, shared by the hero, the download
 * section and the pages that end with a call to action.
 */
export const getTaffy = {
  playHref: links.googlePlay,
  playBadgeAlt: "Get it on Google Play",
  apkLabel: "Download the APK",
  apkHref: links.releases,
  apkNote: "Both are free. The APK comes from GitHub Releases.",
  sourceLabel: "Read the source on GitHub",
  sourceHref: links.repository,
} as const;

export const footer = {
  tagline: "A browser that does the busywork with you.",
  navLabel: "Site",
  links: [
    { label: "Errands", href: "/product/" },
    { label: "Built for phones", href: "/built-for-phones/" },
    { label: "Privacy", href: "/privacy/" },
    { label: "Delete your data", href: "/delete-my-data/" },
    { label: "Terms", href: "/terms/" },
    { label: "Licences", href: "/licenses/" },
    { label: "Contact", href: "/contact/" },
    { label: "GitHub", href: links.repository },
  ],
  publisher: "© 2026 Matterward Labs Private Limited.",
  chromium: "TaffyGo is built on the Chromium open-source project.",
  licence:
    "Its source is under the Mozilla Public License 2.0. The TaffyGo name, " +
    "logo and the Taffy character are not.",
  noTrackers: "This site sets no cookies and uses no trackers.",
} as const;

/** The static page left at the old /privacy-policy/ address. */
export const moved = {
  title: "The privacy policy has moved",
  linkLabel: "Read the privacy policy",
} as const;

export const notFound = {
  title: "Page not found",
  body: "Nothing on this site lives at that address.",
  homeLabel: "Go to the home page",
} as const;

export const themeSwitch = {
  groupLabel: "Colour theme",
  toLight: "Switch to light theme",
  toDark: "Switch to dark theme",
} as const;
