// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/common/public/taffy_product_identity.h"

#include "base/no_destructor.h"

namespace taffy {

namespace {

// The product name, and nothing else that already has an owner.
//
// Deliberately absent from this file, because each already has exactly one
// source of truth and a second copy here would be a defect:
//
//   * the application id and the manifest, owned by
//     //taffy/app/android;
//   * the product version, owned by Chromium's own version_info;
//   * every toolchain and revision pin, owned by TOOLCHAIN.md and its machine
//     owner files.
constexpr char kProductName[] = "TaffyGo";

// Empty permanently, by decision 0130, and no longer empty-pending-a-decision.
//
// The user agent string is frozen by ReduceUserAgentMinorVersion to bytes that
// every Chromium derivative on Android sends, and that freeze is upstream
// removing passive fingerprinting entropy. Putting a product token back into it
// would return that entropy, on every request to every site, and it is
// compatibility-visible besides — sites sniff the string, and the parity matrix
// promises them upstream behaviour.
//
// The product names itself where the platform provides for it instead: the
// client-hint brand list, via AddProductBrand in
// //taffy/browser/product_user_agent_brand.h. Read that file rather than
// putting a value here.
constexpr char kUserAgentProductToken[] = "";

// The complete third-party attribution set that ships with the package.
// Chromium is BSD licensed with additional third-party licenses and the fork
// inherits every one of those obligations (chromium/README.md).
//
// VERIFY AT SP-01: the generated attribution resource path for a downstream
// Android product target at the pinned milestone. The upstream generator is
// what produces the file; this constant only names where the browser process
// looks for it.
constexpr char kAttributionResourcePath[] = "chrome://credits";

}  // namespace

const ProductIdentity& GetProductIdentity() {
  static const base::NoDestructor<ProductIdentity> identity([] {
    ProductIdentity value;
    value.product_name = kProductName;
    value.user_agent_product_token = kUserAgentProductToken;
    // True because this file is compiled only into the TaffyGo product target.
    // The clean-host upstream baseline used to prove the toolchain does not
    // include //taffy at all, so it cannot reach this code and
    // cannot accidentally report itself as branded.
    value.is_taffy_branded = true;
    value.attribution_resource_path = kAttributionResourcePath;
    return value;
  }());
  return *identity;
}

}  // namespace taffy
