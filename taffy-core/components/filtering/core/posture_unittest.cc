// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/core/posture.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::filtering {
namespace {

TEST(FilteringPostureTest, TheMasterToggleWins) {
  const FilteringPosture off(false, {});
  EXPECT_FALSE(off.ActiveForHost("news.example"));
  const FilteringPosture on(true, {});
  EXPECT_TRUE(on.ActiveForHost("news.example"));
}

TEST(FilteringPostureTest, AnExceptionCoversItsHostAndItsSubdomains) {
  const FilteringPosture posture(true, {"example.test"});
  EXPECT_FALSE(posture.ActiveForHost("example.test"));
  EXPECT_FALSE(posture.ActiveForHost("sub.example.test"));
  EXPECT_FALSE(posture.ActiveForHost("a.b.example.test"));
  EXPECT_TRUE(posture.ActiveForHost("other.test"));
  // A suffix that is not a label boundary is a different site.
  EXPECT_TRUE(posture.ActiveForHost("notexample.test"));
}

TEST(FilteringPostureTest, AHostlessDocumentIsFilteredWhileEnabled) {
  const FilteringPosture posture(true, {"example.test"});
  EXPECT_TRUE(posture.ActiveForHost(""));
}

TEST(FilteringPostureTest, AnExceptionIsRecordedEvenWhileTheToggleIsOff) {
  // The sheet needs both facts separately: "off for TaffyGo" and "off for this
  // site" are different sentences, and turning the master toggle back on must
  // not silently forget which sites were allowed (decision 0128).
  const FilteringPosture posture(false, {"example.test"});
  EXPECT_FALSE(posture.ActiveForHost("example.test"));
  EXPECT_FALSE(posture.ActiveForHost("other.test"));
  EXPECT_TRUE(posture.ExceptedForHost("example.test"));
  EXPECT_TRUE(posture.ExceptedForHost("sub.example.test"));
  EXPECT_FALSE(posture.ExceptedForHost("other.test"));
}

TEST(FilteringPostureTest, ExceptedForHostFollowsTheSameLabelBoundary) {
  const FilteringPosture posture(true, {"example.test"});
  EXPECT_TRUE(posture.ExceptedForHost("example.test"));
  EXPECT_TRUE(posture.ExceptedForHost("a.b.example.test"));
  EXPECT_FALSE(posture.ExceptedForHost("notexample.test"));
  // A document with no host is not a site anybody can have allowed.
  EXPECT_FALSE(posture.ExceptedForHost(""));
}

TEST(FilteringPostureTest, TheCoverageRuleIsExactAboutBoundaries) {
  EXPECT_TRUE(HostCoveredByException("example.test", "example.test"));
  EXPECT_TRUE(HostCoveredByException("sub.example.test", "example.test"));
  EXPECT_FALSE(HostCoveredByException("example.test", "sub.example.test"));
  EXPECT_FALSE(HostCoveredByException("notexample.test", "example.test"));
  EXPECT_FALSE(HostCoveredByException("example.test", ""));
}

}  // namespace
}  // namespace taffy::filtering
