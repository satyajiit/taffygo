// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_BUNDLED_ASSET_SOURCE_H_
#define TAFFY_BROWSER_ASSETS_BUNDLED_ASSET_SOURCE_H_

#include <memory>
#include <string_view>

#include "base/files/file_path.h"
#include "base/files/memory_mapped_file.h"

namespace taffy {

// Where the package keeps the artifacts decision 0202 ships inside it.
//
// An interface with two implementations, and the reason is that the two
// hosts do not agree about what a package is. On a phone the bytes are a
// region of the APK reached through `base::android::OpenApkAsset`, which is
// the same door `ReadBundledFilterListsBlocking` already uses for the
// uncompressed filter-list asset. On the host where every suite runs there is
// no APK at all, so a directory stands in for one.
//
// Every method blocks and runs on a `base::MayBlock()` sequence.
class BundledAssetSource {
 public:
  virtual ~BundledAssetSource();

  // Maps one artifact read-only, or answers null when the package does not
  // carry it. `apk_asset_path` is the form `OpenApkAsset` takes, including
  // the leading `assets/`, which is what the generated rows record.
  //
  // Null is not an error here. A build that has not been repackaged since the
  // rows changed is exactly this case, and the seeder's job is to leave the
  // artifact to the delivery plane rather than to fail a launch over it.
  virtual std::unique_ptr<base::MemoryMappedFile> Map(
      std::string_view apk_asset_path) = 0;
};

// The package this binary was installed from.
//
// On a host build every `Map` answers null, because there is no package to
// ask and pretending otherwise would make a seeded store look reachable from
// a place it is not.
class ApkBundledAssetSource : public BundledAssetSource {
 public:
  ApkBundledAssetSource();
  ~ApkBundledAssetSource() override;

  std::unique_ptr<base::MemoryMappedFile> Map(
      std::string_view apk_asset_path) override;
};

// A directory standing in for a package, for a host suite.
//
// It resolves the whole `assets/...` path beneath `root`, rather than taking
// the last segment, so a fixture has the shape the APK has and a test that
// passes here is a test of the same string the device resolves.
class DirectoryBundledAssetSource : public BundledAssetSource {
 public:
  explicit DirectoryBundledAssetSource(base::FilePath root);
  ~DirectoryBundledAssetSource() override;

  std::unique_ptr<base::MemoryMappedFile> Map(
      std::string_view apk_asset_path) override;

 private:
  const base::FilePath root_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_BUNDLED_ASSET_SOURCE_H_
