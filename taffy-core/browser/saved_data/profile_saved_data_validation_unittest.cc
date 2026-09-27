// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/saved_data/profile_saved_data_validation.h"

#include <string>

#include "base/time/time.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::saved_data_internal {
namespace {

namespace mojom = core_service::mojom;

TEST(ProfileSavedDataValidationTest, SignInMetadataIsStrictlyBounded) {
  EXPECT_TRUE(IsValidSignIn("example.test", "person@example.test"));
  EXPECT_TRUE(IsValidSignIn("example.test", ""));
  EXPECT_FALSE(IsValidSignIn("", "person@example.test"));
  EXPECT_FALSE(IsValidSignIn("example.test\nforged.test", "person"));
  EXPECT_FALSE(IsValidSignIn("example.test", "person\tsecret"));
  EXPECT_FALSE(IsValidSignIn(
      std::string(mojom::kMaxSavedSignInSiteBytes + 1u, 's'), "person"));
  EXPECT_FALSE(IsValidSignIn("example.test", std::string("\xc3\x28", 2u)));
}

TEST(ProfileSavedDataValidationTest, DetailsRequireContentAndBoundEveryField) {
  ProfileSavedDataBroker::SavedDetailInput input;
  EXPECT_FALSE(IsValidDetail(input));

  input.given_name = "Asha";
  input.address = "12 First Street\nPune";
  EXPECT_TRUE(IsValidDetail(input));

  input.phone = "555\t0100";
  EXPECT_FALSE(IsValidDetail(input));
  input.phone.clear();
  input.email.assign(mojom::kMaxSavedDetailEmailBytes + 1u, 'e');
  EXPECT_FALSE(IsValidDetail(input));
}

TEST(ProfileSavedDataValidationTest, OpaqueIdsAndTimesCarryNoSourceMaterial) {
  const std::string first = NewOpaqueId("saved-detail-");
  const std::string second = NewOpaqueId("saved-detail-");
  ASSERT_FALSE(first.empty());
  EXPECT_EQ(0u, first.find("saved-detail-"));
  EXPECT_NE(first, second);
  EXPECT_EQ(0u, LastUsedMillis(base::Time()));
  EXPECT_EQ(1u, LastUsedMillis(base::Time::FromMillisecondsSinceUnixEpoch(1)));
}

}  // namespace
}  // namespace taffy::saved_data_internal
