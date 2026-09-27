// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { MetadataRoute } from "next";
import { absoluteUrl, routes } from "@/lib/site";

export const dynamic = "force-static";

/**
 * Priorities follow how much a visitor needs the page: the landing page
 * carries the product, the feature pages explain it, contact is a utility,
 * and the legal pages exist to be found, not to rank.
 */
const PAGE_RANK: Record<string, { changeFrequency: "weekly" | "monthly" | "yearly"; priority: number }> = {
  "/": { changeFrequency: "weekly", priority: 1 },
  "/product/": { changeFrequency: "monthly", priority: 0.9 },
  "/built-for-phones/": { changeFrequency: "monthly", priority: 0.8 },
  "/contact/": { changeFrequency: "yearly", priority: 0.5 },
  "/privacy/": { changeFrequency: "yearly", priority: 0.4 },
  "/delete-my-data/": { changeFrequency: "yearly", priority: 0.4 },
  "/terms/": { changeFrequency: "yearly", priority: 0.2 },
  "/licenses/": { changeFrequency: "yearly", priority: 0.2 },
};

export default function sitemap(): MetadataRoute.Sitemap {
  return routes.map((route) => ({
    url: absoluteUrl(route),
    changeFrequency: PAGE_RANK[route]?.changeFrequency ?? "yearly",
    priority: PAGE_RANK[route]?.priority ?? 0.2,
  }));
}
