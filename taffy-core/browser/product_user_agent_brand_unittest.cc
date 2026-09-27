// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/product_user_agent_brand.h"

#include <string>

#include "base/version_info/version_info.h"
#include "taffy/common/public/taffy_product_identity.h"
#include "testing/gtest/include/gtest/gtest.h"

// Decision 0130: the product names itself in the client-hint brand list and
// never in the user agent string. These hold down the three properties that
// make the upstream hook safe to call, because the hook itself is three lines
// and can hold no property of its own.

namespace taffy {
namespace {

int Named(const blink::UserAgentBrandList& list, const std::string& brand) {
  int count = 0;
  for (const blink::UserAgentBrandVersion& entry : list) {
    if (entry.brand == brand) {
      ++count;
    }
  }
  return count;
}

const blink::UserAgentBrandVersion* Find(const blink::UserAgentBrandList& list,
                                         const std::string& brand) {
  for (const blink::UserAgentBrandVersion& entry : list) {
    if (entry.brand == brand) {
      return &entry;
    }
  }
  return nullptr;
}

// The shape upstream's GenerateBrandVersionList hands the embedder: a greased
// entry and the base project, already shuffled.
blink::UserAgentMetadata UpstreamMetadata(bool with_full_versions) {
  blink::UserAgentMetadata metadata;
  metadata.brand_version_list.emplace_back("Not?A_Brand", "24");
  metadata.brand_version_list.emplace_back(
      "Chromium", version_info::GetMajorVersionNumber());
  if (with_full_versions) {
    metadata.brand_full_version_list.emplace_back("Not?A_Brand", "24.0.0.0");
    metadata.brand_full_version_list.emplace_back(
        "Chromium", std::string(version_info::GetVersionNumber()));
  }
  return metadata;
}

TEST(ProductUserAgentBrandTest, NamesTheProductAtChromiumsVersion) {
  blink::UserAgentMetadata metadata = UpstreamMetadata(true);
  const std::string& name = GetProductIdentity().product_name;

  AddProductBrand(metadata);

  const blink::UserAgentBrandVersion* major =
      Find(metadata.brand_version_list, name);
  ASSERT_NE(nullptr, major);
  EXPECT_EQ(version_info::GetMajorVersionNumber(), major->version);

  const blink::UserAgentBrandVersion* full =
      Find(metadata.brand_full_version_list, name);
  ASSERT_NE(nullptr, full);
  EXPECT_EQ(std::string(version_info::GetVersionNumber()), full->version);
}

// A site that reads the base project still gets its answer. Naming the product
// is an addition, never a replacement — that is the whole reason the brand list
// is a safe place to do this and the user agent string is not.
TEST(ProductUserAgentBrandTest, LeavesTheUpstreamEntriesAlone) {
  blink::UserAgentMetadata metadata = UpstreamMetadata(true);

  AddProductBrand(metadata);

  EXPECT_EQ(1, Named(metadata.brand_version_list, "Chromium"));
  EXPECT_EQ(1, Named(metadata.brand_version_list, "Not?A_Brand"));
  EXPECT_EQ(1, Named(metadata.brand_full_version_list, "Chromium"));
}

// GetUserAgentMetadata(only_low_entropy_ch=true) returns with the full version
// list empty on purpose. Filling it here would hand a caller that asked for low
// entropy alone the high-entropy hint it declined.
TEST(ProductUserAgentBrandTest, LeavesALowEntropyRequestLowEntropy) {
  blink::UserAgentMetadata metadata = UpstreamMetadata(false);

  AddProductBrand(metadata);

  EXPECT_EQ(1, Named(metadata.brand_version_list,
                     GetProductIdentity().product_name));
  EXPECT_TRUE(metadata.brand_full_version_list.empty());
}

// The caller is an embedder hook. An embedder hook that runs twice over one
// metadata must not name the product twice, because "TaffyGo" appearing twice
// in Sec-CH-UA is a malformed header rather than a louder one.
TEST(ProductUserAgentBrandTest, NamesTheProductOnceWhenCalledTwice) {
  blink::UserAgentMetadata metadata = UpstreamMetadata(true);
  const std::string& name = GetProductIdentity().product_name;

  AddProductBrand(metadata);
  AddProductBrand(metadata);

  EXPECT_EQ(1, Named(metadata.brand_version_list, name));
  EXPECT_EQ(1, Named(metadata.brand_full_version_list, name));
}

// The string is the other half of decision 0130, and this is where the "and
// not in the user agent string" clause is held down: the token stays empty, so
// no upstream user agent hook has anything to splice in.
TEST(ProductUserAgentBrandTest, TheUserAgentStringTokenStaysEmpty) {
  EXPECT_TRUE(GetProductIdentity().user_agent_product_token.empty());
}

}  // namespace
}  // namespace taffy
