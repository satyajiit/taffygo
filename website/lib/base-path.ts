// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/**
 * The path the export is served under. Empty for https://taffygo.com, and
 * "/taffygo" for the GitHub Pages project address that serves the site until
 * the custom domain points at it. The Pages workflow sets
 * NEXT_PUBLIC_BASE_PATH from the repository variable SITE_BASE_PATH.
 *
 * next.config.ts and every root-absolute path in the pages read it through
 * this module, so the two builds cannot disagree about where the site lives.
 */
export function normalizeBasePath(raw: string | undefined): string {
  const trimmed = (raw ?? "").trim().replace(/\/+$/, "");
  if (trimmed === "") return "";
  return trimmed.startsWith("/") ? trimmed : `/${trimmed}`;
}

export const BASE_PATH = normalizeBasePath(process.env.NEXT_PUBLIC_BASE_PATH);

/**
 * A root-absolute path as the browser must request it: "/screens/x.webp"
 * becomes "/taffygo/screens/x.webp" under a base path. Used for files in
 * public/ and for links between pages, because the site uses plain anchors
 * rather than next/link, which would add the base path itself.
 */
export function withBasePath(path: string): string {
  if (!path.startsWith("/") || path.startsWith("//")) {
    throw new Error(`withBasePath expects a root-absolute path, got "${path}"`);
  }
  return `${BASE_PATH}${path}`;
}

/**
 * An href taken from content: a path on this site gains the base path, and
 * an address on another site passes through unchanged.
 */
export function siteHref(href: string): string {
  return href.startsWith("/") && !href.startsWith("//") ? withBasePath(href) : href;
}
