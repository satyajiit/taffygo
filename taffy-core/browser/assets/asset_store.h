// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_ASSET_STORE_H_
#define TAFFY_BROWSER_ASSETS_ASSET_STORE_H_

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"

namespace taffy {

// Where one profile's assets live on disk, and the only code that decides.
//
// Every method here blocks, so every one runs on a `base::MayBlock()` sequence
// and none of them is called from the UI thread. The class holds no state
// beyond the root it was given, which is what lets a test point it at a
// temporary directory and get the real implementation rather than a double.
//
// The layout is deliberate in two ways. A staged transfer and an installed
// artifact are in different directories, so a partial file can never be opened
// as a finished one by anything that walks the tree. And an installed artifact
// is under its own revision, so two revisions coexist while one is being
// replaced and an interrupted upgrade leaves the old one working.
//
//   <root>/staging/<asset-id>@<revision>.part
//   <root>/installed/<asset-id>/<revision>/artifact
//
// Identity and revision reach the path through `IsSafeComponent`, which refuses
// anything that is not the alphabet the catalog generator already enforces.
// Refusing rather than escaping is the point: a component that needed escaping
// did not come from the catalog.
class AssetStore {
 public:
  // What the store found for one asset when it looked.
  struct Found {
    // The asset's identity.
    std::string asset_id;
    // The revision found.
    std::string asset_revision;
    // Bytes staged but not installed; zero when there is no partial file.
    uint64_t staged_bytes = 0;
    // Whether a finished artifact is present.
    bool installed = false;
    // Bytes the installed artifact occupies; zero when it is absent.
    uint64_t installed_bytes = 0;
  };

  // `root` is the profile-scoped directory the store owns entirely.
  //
  // An empty root is a store with nowhere to put anything: every read answers
  // nothing and every write refuses, rather than resolving against whatever
  // the working directory happens to be. That is what a build or a test with
  // no asset directory should get, and it is checked here rather than at each
  // of the six places that would otherwise have to remember.
  explicit AssetStore(base::FilePath root);
  AssetStore(const AssetStore&) = delete;
  AssetStore& operator=(const AssetStore&) = delete;
  ~AssetStore();

  // Whether a component may become a path segment.
  //
  // Lowercase letters, digits, and the three separators a revision uses, with
  // no leading or trailing separator and no repeat. That is a subset of what a
  // path segment permits: it cannot be `.` or `..`, cannot introduce a second
  // segment, and cannot need escaping.
  static bool IsSafeComponent(std::string_view value);

  // Every asset the store holds, staged or installed. Blocks.
  //
  // A file whose name does not decode to a safe identity and revision is
  // ignored rather than reported: the store did not write it, so it is not the
  // store's to describe. It is also not deleted, because deleting a file
  // nobody understands is how a bug becomes data loss.
  std::vector<Found> Scan() const;

  // Opens the staging file for one asset, positioned to append. Blocks.
  //
  // `resume_from` is the offset the caller intends to continue from. The file
  // is truncated to it, so a caller that asks to resume from a smaller offset
  // than the file holds gets exactly what it asked for rather than a file with
  // a gap. An invalid file is returned when the identity is unsafe, the
  // directory cannot be made, or the file cannot be opened.
  base::File OpenStaging(std::string_view asset_id,
                         std::string_view asset_revision,
                         uint64_t resume_from) const;

  // How many bytes are staged for one asset, or zero. Blocks.
  uint64_t StagedBytes(std::string_view asset_id,
                       std::string_view asset_revision) const;

  // Moves the staged file to its installed place. Blocks.
  //
  // The move is what makes an install atomic: a reader either sees no
  // installed artifact or sees a complete one, and never sees a partial file
  // under the installed name. False means nothing moved and the staged file is
  // still there.
  bool Commit(std::string_view asset_id, std::string_view asset_revision);

  // Deletes everything for one asset, staged and installed. Blocks.
  //
  // Answers how many bytes were freed, which is what a person is shown. A
  // removal of something that was not there is not a failure and frees zero.
  uint64_t Remove(std::string_view asset_id, std::string_view asset_revision);

  // The revision of one asset that is installed, or empty. Blocks.
  //
  // At most one is expected — an install replaces the last one — but two can
  // coexist for as long as a replacement takes, and an interrupted upgrade can
  // leave both. The newest by name wins, which is the same order `Scan` walks
  // and the same order the revision strings sort in.
  std::string InstalledRevision(std::string_view asset_id) const;

  // The installed artifact, opened read-only, or an invalid file. Blocks.
  //
  // This is what a worker eventually receives — a descriptor, never a path,
  // which is what decision 0041 requires and what the sandbox would enforce
  // regardless.
  base::File OpenInstalled(std::string_view asset_id,
                           std::string_view asset_revision) const;

  // The root, for a test that wants to inspect the tree it made.
  const base::FilePath& root_for_testing() const { return root_; }

 private:
  base::FilePath StagingPath(std::string_view asset_id,
                             std::string_view asset_revision) const;
  base::FilePath InstalledDirectory(std::string_view asset_id,
                                    std::string_view asset_revision) const;
  base::FilePath InstalledPath(std::string_view asset_id,
                               std::string_view asset_revision) const;

  const base::FilePath root_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_ASSET_STORE_H_
