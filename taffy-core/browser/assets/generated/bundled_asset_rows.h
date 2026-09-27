// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// GENERATED FILE — DO NOT EDIT.
//
// Written by taffy-core/components/delivery/core/rust/asset-plane/
// catalog/tools/generate_bundled_assets.py from that catalog's source
// `bundled` block. Change the block and regenerate; an edit here is
// overwritten and its `--check` fails first.

#ifndef TAFFY_BROWSER_ASSETS_GENERATED_BUNDLED_ASSET_ROWS_H_
#define TAFFY_BROWSER_ASSETS_GENERATED_BUNDLED_ASSET_ROWS_H_

#include <stdint.h>

#include <array>
#include <string_view>

namespace taffy {

// One artifact the package carries, and everything needed to install
// it without asking anything outside this binary.
struct BundledAssetRow {
  // The identity and revision the delivery store installs it under.
  std::string_view asset_id;
  std::string_view asset_revision;
  // What to ask the package for, in `base::android::OpenApkAsset` form.
  std::string_view apk_asset_path;
  // What the catalogue pins. The seeder refuses anything else rather
  // than installing bytes the core would later plan a repair for.
  uint64_t transfer_bytes;
  std::string_view digest;
};

inline constexpr std::array<BundledAssetRow, 4> kBundledAssetRows = {{
    {
        "easylist-base",
        "202608272347-taffy.1",
        "assets/taffy-delivery/easylist-base.zip",
        1183665u,
        "c1ea235b7eb1de2efa7b6fd6f5af802fa548e779b9e008efe98ff629a16eef10",
    },
    {
        "country-flags",
        "7.5.0-taffy.1",
        "assets/taffy-delivery/flags-4x3-webp.zip",
        584892u,
        "46d06699bd17d6943eeb2d4540b707eae34c6313ac01d1835c61201b2decb5b5",
    },
    {
        "start-scenes",
        "20260910-taffy.1",
        "assets/taffy-delivery/scenes-4x3-webp.zip",
        1219859u,
        "1a6d9eeee5df0725f35d6dfdd6028ee90aa79312b35c4a01d8f856525f40c566",
    },
    {
        "python-stdlib",
        "3.14.7-taffy.1",
        "assets/taffy-delivery/stdlib.zip",
        2769014u,
        "a01d637051448386b07865ca543c8dd3aed00a9e374385aa971de94334fee363",
    },
}};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_GENERATED_BUNDLED_ASSET_ROWS_H_
