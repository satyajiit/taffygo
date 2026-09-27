// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { MetadataRoute } from "next";
import { appTheme } from "@/lib/app-theme";
import { withBasePath } from "@/lib/base-path";
import { site } from "@/lib/site";

export const dynamic = "force-static";

export default function manifest(): MetadataRoute.Manifest {
  return {
    name: site.name,
    short_name: site.name,
    description: site.description,
    start_url: withBasePath("/"),
    display: "browser",
    background_color: appTheme.lightSurface,
    theme_color: appTheme.darkSurface,
    icons: [
      { src: withBasePath("/icon.png"), sizes: "192x192", type: "image/png" },
      { src: withBasePath("/apple-icon.png"), sizes: "180x180", type: "image/png" },
    ],
  };
}
