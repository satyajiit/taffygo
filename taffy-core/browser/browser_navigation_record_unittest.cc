// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/browser_navigation_record.h"

#include <vector>

#include "taffy/components/intelligence/content/origin_codec.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/mock_navigation_handle.h"
#include "net/base/net_errors.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// The record's contract, asserted without a page.
//
// VERIFY AT SP-01: the content::MockNavigationHandle setters this file uses —
// set_url, set_has_committed, set_is_same_document, set_net_error_code and
// set_redirect_chain — and the mocked predicates it stubs. Upstream file to
// read: content/public/test/mock_navigation_handle.h. If a setter is missing
// at the pinned milestone, the coverage moves to
// navigation_lifecycle_tracker_browsertest.cc, which drives a real navigation
// and needs no mock at all; nothing about the record changes.

namespace taffy {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

class BrowserNavigationRecordTest : public testing::Test {
 protected:
  void TearDown() override { OriginCodec::Get().ClearForTesting(); }

  // A handle that committed successfully at `url`, with the mocked lifecycle
  // predicates stubbed to the ordinary case. Each test overrides only the fact
  // it is about.
  std::unique_ptr<NiceMock<content::MockNavigationHandle>> CommittedHandle(
      const GURL& url) {
    auto handle = std::make_unique<NiceMock<content::MockNavigationHandle>>();
    handle->set_url(url);
    handle->set_has_committed(true);
    handle->set_net_error_code(net::OK);
    handle->set_redirect_chain({url});
    // Setters, not ON_CALL: at this pin MockNavigationHandle implements both of
    // these as concrete non-mocked overrides backed by a field
    // (content/public/test/mock_navigation_handle.h), so there is no
    // gmock_ method to bind an expectation to. Set explicitly rather than left
    // to the default, because what this fixture is asserting is the ordinary
    // navigation path, and a reader should see that stated.
    handle->set_is_served_from_bfcache(false);
    handle->set_is_prerendered_page_activation(false);
    return handle;
  }

  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(BrowserNavigationRecordTest, DirectLoadHasAOneEntryOriginChain) {
  const GURL url("https://primary.taffy.test/article/quantum-storage-review");
  auto handle = CommittedHandle(url);

  BrowserNavigationRecord record =
      BrowserNavigationRecord::FromCommittedNavigation(
          handle.get(), TabId{"tab_1"}, FrameId{"frame_1"});

  EXPECT_EQ(url, record.committed_url());
  EXPECT_EQ("https://primary.taffy.test",
            record.final_origin().serialization);
  ASSERT_EQ(1u, record.redirect_origins().size());
  EXPECT_EQ(record.final_origin(), record.redirect_origins().front());
  EXPECT_EQ(0u, record.server_redirect_count());
  EXPECT_FALSE(record.crossed_origin_during_redirect());
  EXPECT_TRUE(record.CarriesRequestedContent());
}

TEST_F(BrowserNavigationRecordTest,
       SameOriginRedirectDoesNotSetTheCrossOriginFlag) {
  const GURL start("https://primary.taffy.test/redirect/start.html");
  const GURL end("https://primary.taffy.test/redirect/arrived.html");
  auto handle = CommittedHandle(end);
  handle->set_redirect_chain({start, end});

  BrowserNavigationRecord record =
      BrowserNavigationRecord::FromCommittedNavigation(
          handle.get(), TabId{"tab_1"}, FrameId{"frame_1"});

  // Both hops share an origin, so the chain collapses to one entry and the
  // policy recheck flag stays clear.
  ASSERT_EQ(1u, record.redirect_origins().size());
  EXPECT_FALSE(record.crossed_origin_during_redirect());
  EXPECT_EQ(1u, record.server_redirect_count());
}

TEST_F(BrowserNavigationRecordTest, CrossOriginRedirectIsVisibleAndFlagged) {
  // PAR-NAV-004: the final origin is visible and permission or model scope is
  // rechecked after a cross-origin redirect. The flag is what the recheck is
  // keyed off, so no consumer has to walk the chain itself.
  const GURL start("https://primary.taffy.test/redirect/start.html");
  const GURL end("https://partner.taffy.test/redirect/arrived.html");
  auto handle = CommittedHandle(end);
  handle->set_redirect_chain({start, end});

  BrowserNavigationRecord record =
      BrowserNavigationRecord::FromCommittedNavigation(
          handle.get(), TabId{"tab_1"}, FrameId{"frame_1"});

  ASSERT_EQ(2u, record.redirect_origins().size());
  EXPECT_EQ("https://primary.taffy.test", record.initial_origin().serialization);
  EXPECT_EQ("https://partner.taffy.test", record.final_origin().serialization);
  EXPECT_TRUE(record.crossed_origin_during_redirect());
}

TEST_F(BrowserNavigationRecordTest, ErrorPageDoesNotCarryRequestedContent) {
  // PAR-NAV-006 requires "no false completion by AI". An error page is a
  // document; a reader that treats it as the requested content is what this
  // predicate exists to stop.
  const GURL url("https://primary.taffy.test/missing");
  auto handle = CommittedHandle(url);
  handle->set_net_error_code(net::ERR_NAME_NOT_RESOLVED);
  handle->set_is_error_page(true);

  BrowserNavigationRecord record =
      BrowserNavigationRecord::FromCommittedNavigation(
          handle.get(), TabId{"tab_1"}, FrameId{"frame_1"});

  EXPECT_EQ(NavigationErrorClass::kDnsFailure,
            record.error_verdict().error_class);
  EXPECT_FALSE(record.CarriesRequestedContent());
}

TEST_F(BrowserNavigationRecordTest, IdentityIsCarriedNotAllocated) {
  // The broker is the single allocator of TabId and FrameId. The record echoes
  // what it is given and never mints one, so two records of the same frame
  // cannot disagree about its name.
  auto handle = CommittedHandle(GURL("https://primary.taffy.test/"));

  BrowserNavigationRecord record =
      BrowserNavigationRecord::FromCommittedNavigation(
          handle.get(), TabId{"tab_7"}, FrameId{"frame_3"});

  EXPECT_EQ("tab_7", record.tab_id().value);
  EXPECT_EQ("frame_3", record.frame_id().value);

  // Identity is optional. A record built before the assistant ever
  // initialised is still authoritative for URL, origin and error state, which
  // is what PAR-AI-BR-001 needs from it.
  BrowserNavigationRecord anonymous =
      BrowserNavigationRecord::FromCommittedNavigation(handle.get(), TabId{},
                                                       FrameId{});
  EXPECT_FALSE(anonymous.tab_id().is_valid());
  EXPECT_EQ(record.committed_url(), anonymous.committed_url());
  EXPECT_EQ(record.final_origin(), anonymous.final_origin());
}

TEST_F(BrowserNavigationRecordTest, TheApiOffersNoWayToWriteAUrl) {
  // The guarantee this record exists for, stated as a compile-time fact rather
  // than as a comment: there is no default constructor and no assignment from
  // anything a renderer could produce. A renderer-reported URL cannot become a
  // committed URL because there is no expression that would do it.
  static_assert(!std::is_default_constructible_v<BrowserNavigationRecord>,
                "A navigation record must come from a committed navigation. A "
                "default-constructible record could be filled from a renderer "
                "message.");
  static_assert(!std::is_constructible_v<BrowserNavigationRecord, GURL>,
                "A navigation record must not be constructible from a URL.");
  static_assert(std::is_copy_constructible_v<BrowserNavigationRecord>,
                "Records are values; the tracker stores them by value.");
}

}  // namespace
}  // namespace taffy
