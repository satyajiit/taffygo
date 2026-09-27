// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/bundled_asset_source.h"

#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/apk_assets.h"
#endif

namespace taffy {

BundledAssetSource::~BundledAssetSource() = default;

ApkBundledAssetSource::ApkBundledAssetSource() = default;
ApkBundledAssetSource::~ApkBundledAssetSource() = default;

std::unique_ptr<base::MemoryMappedFile> ApkBundledAssetSource::Map(
    std::string_view apk_asset_path) {
#if BUILDFLAG(IS_ANDROID)
  base::MemoryMappedFile::Region region;
  const int descriptor =
      base::android::OpenApkAsset(std::string(apk_asset_path), &region);
  if (descriptor < 0) {
    return nullptr;
  }
  auto mapped = std::make_unique<base::MemoryMappedFile>();
  // The region matters and is easy to drop: an APK asset is a span inside a
  // much larger file, so a mapping that ignored it would hash the archive
  // rather than the artifact and refuse every row.
  if (!mapped->Initialize(base::File(descriptor), region)) {
    return nullptr;
  }
  return mapped;
#else
  return nullptr;
#endif
}

DirectoryBundledAssetSource::DirectoryBundledAssetSource(base::FilePath root)
    : root_(std::move(root)) {}

DirectoryBundledAssetSource::~DirectoryBundledAssetSource() = default;

std::unique_ptr<base::MemoryMappedFile> DirectoryBundledAssetSource::Map(
    std::string_view apk_asset_path) {
  base::FilePath path = root_;
  for (const std::string& segment : base::SplitString(
           apk_asset_path, "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    // A traversing or empty segment is refused rather than normalized. The
    // paths here are generated from the catalog and cannot contain one, so a
    // segment like this means the caller is not the generated table and the
    // honest answer is that the package does not carry it.
    if (segment.empty() || segment == "." || segment == "..") {
      return nullptr;
    }
    path = path.AppendASCII(segment);
  }
  auto mapped = std::make_unique<base::MemoryMappedFile>();
  if (!mapped->Initialize(path)) {
    return nullptr;
  }
  return mapped;
}

}  // namespace taffy
