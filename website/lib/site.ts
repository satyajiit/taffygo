// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { Metadata } from "next";

/**
 * Canonical production origin (decision 0068). A build served from the
 * GitHub Pages project address keeps it: canonical links, the sitemap and
 * structured data always name https://taffygo.com.
 */
const CANONICAL_URL = "https://taffygo.com";

const REPOSITORY = "https://github.com/satyajiit/taffygo";

/** Where people get TaffyGo and reach the project. All of it is on GitHub. */
export const links = {
  repository: REPOSITORY,
  releases: `${REPOSITORY}/releases/latest`,
  issues: `${REPOSITORY}/issues`,
  discussions: `${REPOSITORY}/discussions`,
  securityReport: `${REPOSITORY}/security/advisories/new`,
  licence: `${REPOSITORY}/blob/main/LICENSE`,
  notice: `${REPOSITORY}/blob/main/NOTICE`,
  trademarks: `${REPOSITORY}/blob/main/TRADEMARKS.md`,
  googlePlay: "https://play.google.com/store/apps/details?id=com.taffygo.browser",
  githubPrivacy:
    "https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement",
} as const;

export const site = {
  name: "TaffyGo",
  assistant: "Taffy",
  publisher: "Matterward Labs Private Limited",
  title: "TaffyGo, the browser that does the busywork with you",
  description:
    "TaffyGo is a free, open-source Android browser that blocks ads and " +
    "trackers. Its assistant, Taffy, uses the AI provider you connect. No " +
    "account, no TaffyGo server.",
  url: CANONICAL_URL,
  ogImage: "/og-image-2655c535d406.jpg",
  ogImageAlt:
    "The TaffyGo logo beside a painting of Taffy, a small winged character, " +
    "sitting on a desk next to a phone.",
} as const;

/**
 * The per-route SEO contract. Every page exports a `metadata` title and
 * description taken from this record (tests enforce the agreement), so a
 * route's search snippet is edited in exactly one place. Keys are the
 * trailing-slash route paths the static export serves.
 */
export const routeMeta = {
  "/": {
    title: site.title,
    description: site.description,
  },
  "/product/": {
    title: "Hand Taffy an errand | TaffyGo",
    description:
      "One errand in TaffyGo from start to finish, in screens from a phone: " +
      "you pick the pages, Taffy works in its own tabs while you watch, and " +
      "the result names its sources.",
  },
  "/built-for-phones/": {
    title: "Built for phones | TaffyGo",
    description:
      "Back, forward, Ask Taffy and your tabs sit in one row at the bottom, " +
      "and the tabs Taffy opens are grouped apart from yours.",
  },
  "/contact/": {
    title: "Contact | TaffyGo",
    description:
      "Report a bug or ask a question on GitHub, and report a security " +
      "problem privately there. TaffyGo has no support inbox and no " +
      "contact form.",
  },
  "/privacy/": {
    title: "Privacy policy | TaffyGo",
    description:
      "TaffyGo keeps your data on your phone. Matterward Labs runs no server " +
      "for it, collects nothing from the app and has no account for you " +
      "to make.",
  },
  "/delete-my-data/": {
    title: "Delete your data | TaffyGo",
    description:
      "Matterward Labs holds no data about you, so there is nothing on our " +
      "side to delete. This is how to clear what TaffyGo keeps on your phone.",
  },
  "/terms/": {
    title: "Terms of use | TaffyGo",
    description:
      "The terms for using TaffyGo: the source licence, what it leaves out, " +
      "and the services you connect yourself.",
  },
  "/licenses/": {
    title: "Open-source licences | TaffyGo",
    description:
      "TaffyGo's source is under the Mozilla Public License 2.0. Where to " +
      "read the notices for the browser and for this website.",
  },
} as const;

export type RoutePath = keyof typeof routeMeta;

export const routes = Object.keys(routeMeta) as RoutePath[];

/**
 * Old addresses kept alive as static redirect pages (decision 0068 keeps
 * /privacy-policy/). They are not routes: no sitemap entry, no canonical of
 * their own, and noindex.
 */
export const redirects = {
  "/privacy-policy/": "/privacy/",
} as const satisfies Record<string, RoutePath>;

export function absoluteUrl(path: string): string {
  return `${site.url}${path.startsWith("/") ? path : `/${path}`}`;
}

/** Complete route metadata so social cards never inherit the home page copy. */
export function pageMetadata(route: RoutePath): Metadata {
  const page = routeMeta[route];
  const image = {
    url: site.ogImage,
    width: 1200,
    height: 630,
    alt: site.ogImageAlt,
  };
  return {
    title: { absolute: page.title },
    description: page.description,
    alternates: { canonical: route },
    openGraph: {
      type: "website",
      siteName: site.name,
      locale: "en_IN",
      title: page.title,
      description: page.description,
      url: absoluteUrl(route),
      images: [image],
    },
    twitter: {
      card: "summary_large_image",
      title: page.title,
      description: page.description,
      images: [image],
    },
  };
}
