// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/product_user_agent_brand.h"

#include <string>

#include "base/version_info/version_info.h"
#include "taffy/common/public/taffy_product_identity.h"

namespace taffy {

namespace {

bool AlreadyNamed(const blink::UserAgentBrandList& list,
                  const std::string& brand) {
  for (const blink::UserAgentBrandVersion& entry : list) {
    if (entry.brand == brand) {
      return true;
    }
  }
  return false;
}

}  // namespace

void AddProductBrand(blink::UserAgentMetadata& metadata) {
  const ProductIdentity& identity = GetProductIdentity();
  if (!identity.is_taffy_branded || identity.product_name.empty()) {
    return;
  }

  // The brand's version is Chromium's version, which is what upstream's
  // GenerateBrandVersionList already does for a browser brand. It is also what
  // keeps this from inventing a second owner for the product version:
  // //taffy/common/public/taffy_product_identity.h holds none on purpose.
  if (!AlreadyNamed(metadata.brand_version_list, identity.product_name)) {
    metadata.brand_version_list.emplace_back(
        identity.product_name, version_info::GetMajorVersionNumber());
  }

  if (!metadata.brand_full_version_list.empty() &&
      !AlreadyNamed(metadata.brand_full_version_list, identity.product_name)) {
    metadata.brand_full_version_list.emplace_back(
        identity.product_name, std::string(version_info::GetVersionNumber()));
  }
}

}  // namespace taffy
