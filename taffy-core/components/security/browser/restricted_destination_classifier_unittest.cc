// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/security/browser/restricted_destination_classifier.h"

#include <string>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

Origin Tuple(std::string serialization) {
  Origin origin;
  origin.kind = OriginKind::kTuple;
  origin.serialization = std::move(serialization);
  return origin;
}

TEST(RestrictedDestinationClassifierTest, ExactReviewedHostIsRestricted) {
  EXPECT_EQ(ClassifyRestrictedDestination(Tuple("https://mail.google.com")),
            RestrictedDestinationStatus::kRestricted);
}

TEST(RestrictedDestinationClassifierTest, SubdomainsDoNotInheritAClass) {
  EXPECT_EQ(
      ClassifyRestrictedDestination(Tuple("https://evil.mail.google.com")),
      RestrictedDestinationStatus::kNotListed);
}

TEST(RestrictedDestinationClassifierTest, OrdinaryHostIsOnlyNotListed) {
  EXPECT_EQ(ClassifyRestrictedDestination(Tuple("https://example.com")),
            RestrictedDestinationStatus::kNotListed);
}

TEST(RestrictedDestinationClassifierTest, UnnormalizedOrOpaqueOriginRefuses) {
  EXPECT_EQ(ClassifyRestrictedDestination(Tuple("https://MAIL.google.com")),
            RestrictedDestinationStatus::kInvalidOrigin);
  Origin opaque;
  opaque.kind = OriginKind::kOpaque;
  opaque.opaque_id = "opq_1";
  EXPECT_EQ(ClassifyRestrictedDestination(opaque),
            RestrictedDestinationStatus::kInvalidOrigin);
}

}  // namespace
}  // namespace taffy
