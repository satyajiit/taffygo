// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { defineConfig, globalIgnores } from "eslint/config";
import next from "eslint-config-next";

export default defineConfig([
  globalIgnores([".next/**", "out/**", "next-env.d.ts"]),
  next,
  {
    settings: {
      // Pinned rather than detected: eslint-plugin-react's detection path
      // reads package metadata that is not resolvable from a workspace store.
      react: { version: "19.2" },
    },
    rules: {
      // The site is exported as plain files with no image server, and each
      // logo is an asset whose exact bytes are committed, so <picture>/<img>
      // is the right element.
      "@next/next/no-img-element": "off",
      // Four static pages, no client router: plain anchors keep the emitted
      // href byte-identical to the exported directory (trailingSlash), which
      // next/link rewrites.
      "@next/next/no-html-link-for-pages": "off",
    },
  },
]);
