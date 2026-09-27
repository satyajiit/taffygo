// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_ASSET_PACK_H_
#define TAFFY_BROWSER_ASSETS_ASSET_PACK_H_

#include <stddef.h>
#include <stdint.h>

#include <string_view>
#include <vector>

namespace taffy {

class AssetStore;

// How a read of one member out of an installed pack ended.
//
// Every value is something the browser observed about a file it owns. There is
// no verdict here for "the core refused", because the core is not asked: the
// bytes are on the device and were checked against the catalog's digest before
// they were installed, so reading one of them decides nothing.
enum class PackMemberVerdict {
  kOk,
  // No installed revision of that asset. The ordinary answer before a download
  // has finished, and the reason this is a verdict rather than an error.
  kNotInstalled,
  kNotFound,
  kTooLarge,
  // Installed, and not the container it was expected to be. The catalog says
  // what each asset is packed as and the catalog lives in the isolated core, so
  // this process can only try and report what happened.
  kUnreadable,
};

// One member's bytes, and how the read ended.
struct PackMemberResult {
  PackMemberVerdict verdict = PackMemberVerdict::kNotInstalled;
  std::vector<uint8_t> bytes;
};

// Reads one member out of the installed revision of `asset_id`. Blocks.
//
// Whichever revision is installed, not one the caller names. A caller asking
// for a member wants what is on the device; naming a revision would let it ask
// for one that is not there and get a failure it could do nothing about.
//
// `member_path` is matched against the archive's own normalized entry paths, so
// an entry stored as `../x` or `/x` cannot be reached by asking for `x`. It is
// also refused here before the archive is opened, because a caller that needed
// to escape did not get the name from a manifest.
//
// Both bounds are the contract's, passed in rather than repeated here, so this
// file cannot drift from the interface that admits the call. A member past
// `max_bytes` is `kTooLarge` and no partial content is returned: half an image
// is not a smaller image.
PackMemberResult ReadPackMember(const AssetStore& store,
                                std::string_view asset_id,
                                std::string_view member_path,
                                size_t max_path_bytes,
                                size_t max_bytes);

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_ASSET_PACK_H_
