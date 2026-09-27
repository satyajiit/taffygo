// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_BUNDLED_ASSET_SEEDER_H_
#define TAFFY_BROWSER_ASSETS_BUNDLED_ASSET_SEEDER_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "taffy/browser/assets/bundled_asset_source.h"
#include "taffy/browser/assets/generated/bundled_asset_rows.h"

namespace taffy {

// What one seeding pass did, per artifact.
//
// Counts rather than a bool, because every one of these is a state the
// product goes on running in and only one of them is an error. A launch is
// never failed over any of them: the delivery plane is still there, and an
// artifact that is not seeded is one the core may plan a transfer for.
struct BundledSeedOutcome {
  // The store already held this exact revision. The ordinary second launch.
  int already_installed = 0;
  // Installed from the package by this pass.
  int seeded = 0;
  // The package does not carry it. A build whose assets were not repackaged
  // after the rows moved is exactly this, and it is not a failure here.
  int absent = 0;
  // The package carries it and its bytes are not what the catalogue pins, or
  // the store refused the write. Nothing was installed.
  int refused = 0;
  // The identities this pass installed, in row order.
  //
  // Counts say how it went; these say what changed, and something has to.
  // `ProfilePythonLibrary` scans the store in its own constructor and caches
  // the answer, and `FilteringRulesetService` is built with a reader over the
  // same store — both before a profile's core is ever bootstrapped, which is
  // when seeding happens. An install nothing announces is therefore invisible
  // for the life of the process rather than late: the library stays absent,
  // and `StartPageGate` holds the start page closed on exactly the artifact
  // this seeding exists to deliver.
  std::vector<std::string> seeded_ids;
};

// Installs the artifacts the package carries into the delivery store.
//
// Decision 0202 ships every required part inside the app, so on a first run
// the store is empty and the catalogue's required rows are all satisfiable
// without a network. This is what satisfies them, and it has to happen before
// the store is scanned for the core's bootstrap: the scan is what tells the
// core what the device has, and an artifact seeded after it would be invisible
// for the life of that generation while the core planned a download for it
// against an origin that is empty.
//
// Blocks. Every call is on a `base::MayBlock()` sequence.
//
// The digest is checked before anything is installed, not after. The bytes are
// mapped, hashed and compared against the row, and only then written and
// committed — so a package whose asset does not match the compiled rows leaves
// the store exactly as it was rather than leaving an artifact the delivery
// plane would have to notice and repair.
BundledSeedOutcome SeedBundledAssets(
    const base::FilePath& store_root,
    BundledAssetSource& source,
    base::span<const BundledAssetRow> rows = base::span(kBundledAssetRows));

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_BUNDLED_ASSET_SEEDER_H_
