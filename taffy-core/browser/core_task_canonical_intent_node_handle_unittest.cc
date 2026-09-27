// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "taffy/browser/core_task_canonical_intent.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

// The live document the policy gate is about to bind a press or a focus to.
constexpr char kTab[] = "tab_1";
constexpr char kNode[] = "n193";
constexpr char kFrame[] = "frame_1";
constexpr char kEpoch[] = "epoch_1";
constexpr uint64_t kLiveRevision = 654u;
constexpr char kOrigin[] = "https://portal.example";

CanonicalObservedNodeHandle ReadAt(uint64_t revision) {
  return CanonicalObservedNodeHandle{
      .tab_id = kTab,
      .frame_id = kFrame,
      .page_epoch = kEpoch,
      .graph_revision = revision,
      .node_id = kNode,
      .expected_origin = kOrigin,
  };
}

const char* Mismatch(const CanonicalObservedNodeHandle& handle) {
  return ObservedNodeHandleMismatch(handle, kTab, kNode, kFrame, kEpoch,
                                    kLiveRevision, kOrigin);
}

// Decision 0188: a phone refused every press on a portal that redraws itself,
// because the page's revision had moved past the reading the model pressed
// from. The node having changed is the renderer's to refuse; the page having
// changed somewhere else is not a reason.
TEST(ObservedNodeHandleMismatchTest, AHandleMayBeOlderThanTheLivePage) {
  EXPECT_EQ(nullptr, Mismatch(ReadAt(kLiveRevision)));
  EXPECT_EQ(nullptr, Mismatch(ReadAt(86u)));
  EXPECT_EQ(nullptr, Mismatch(ReadAt(1u)));
}

TEST(ObservedNodeHandleMismatchTest, ARevisionNobodyReportedIsRefused) {
  EXPECT_STREQ("revision-from-the-future",
               Mismatch(ReadAt(kLiveRevision + 1u)));
  EXPECT_STREQ("no-revision", Mismatch(ReadAt(0u)));
}

TEST(ObservedNodeHandleMismatchTest, EveryOtherClauseIsExact) {
  CanonicalObservedNodeHandle tab = ReadAt(86u);
  tab.tab_id = "tab_2";
  EXPECT_STREQ("tab", Mismatch(tab));

  CanonicalObservedNodeHandle node = ReadAt(86u);
  node.node_id = "n194";
  EXPECT_STREQ("node", Mismatch(node));

  CanonicalObservedNodeHandle frame = ReadAt(86u);
  frame.frame_id = "frame_2";
  EXPECT_STREQ("frame", Mismatch(frame));

  // A handle from a document the tab has since left names a page that no
  // longer exists, however old or new its revision.
  CanonicalObservedNodeHandle epoch = ReadAt(86u);
  epoch.page_epoch = "epoch_2";
  EXPECT_STREQ("page-epoch", Mismatch(epoch));

  CanonicalObservedNodeHandle opaque = ReadAt(86u);
  opaque.expected_origin_is_opaque = true;
  opaque.expected_origin.clear();
  EXPECT_STREQ("opaque-origin", Mismatch(opaque));

  CanonicalObservedNodeHandle origin = ReadAt(86u);
  origin.expected_origin = "https://other.example";
  EXPECT_STREQ("origin", Mismatch(origin));
}

}  // namespace
}  // namespace taffy
