// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { NextConfig } from "next";
import { BASE_PATH } from "./lib/base-path";

/**
 * Static export only: the site is uploaded as plain files and has no server,
 * no revalidation and no backend. See docs/website/landing-page.md.
 *
 * The base path is empty for https://taffygo.com and "/taffygo" for the
 * GitHub Pages project address (NEXT_PUBLIC_BASE_PATH). Next applies it to
 * its own /_next/ files; lib/base-path.ts applies it to everything else.
 */
const nextConfig: NextConfig = {
  output: "export",
  trailingSlash: true,
  images: { unoptimized: true },
  reactStrictMode: true,
  poweredByHeader: false,
  ...(BASE_PATH ? { basePath: BASE_PATH, assetPrefix: BASE_PATH } : {}),
};

export default nextConfig;
