// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/browsing_projection.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>

#include "taffy/contracts/browsing/generated/cpp/browsing_enums.h"
#include "testing/gtest/include/gtest/gtest.h"

// What this suite is for, and what it deliberately is not.
//
// Totality is not asserted here, because the compiler asserts it: every switch
// in browsing_projection.cc is exhaustive with no `default`, so an enumerator
// added to one of this directory's enumerations does not reach this file — it
// stops the build. What a test can say that a compiler cannot is *which*
// contract value each browser fact was mapped to, and that is a decision worth
// writing down: the difference between a refusal reported as `unavailable` and
// one reported as `refused by policy` is what a person is told.
//
// The second half asserts the contract's own decoders against the same values,
// so the projection and the decoder cannot drift into disagreeing about what a
// wire integer means. Without it the two halves of the seam are checked
// separately and their agreement is assumed.
namespace taffy {
namespace {

namespace mojom = browsing::mojom;
namespace wire = browsing::wire;
namespace project = browsing_projection;

TEST(BrowsingProjectionTest, ANavigationFailureKeepsItsCategory) {
  EXPECT_EQ(project::Project(NavigationErrorClass::kNone),
            mojom::NavigationFailure::kNone);
  EXPECT_EQ(project::Project(NavigationErrorClass::kDnsFailure),
            mojom::NavigationFailure::kDnsFailure);
  // Offline and a lost connection stay apart across the seam. They are the
  // pair a surface gives different retry advice for, and the classifier keeps
  // them apart for exactly that reason.
  EXPECT_EQ(project::Project(NavigationErrorClass::kOffline),
            mojom::NavigationFailure::kOffline);
  EXPECT_EQ(project::Project(NavigationErrorClass::kConnectionFailure),
            mojom::NavigationFailure::kConnectionFailure);
  // Safe Browsing is never folded into the general client-blocked class. The
  // parity row exists to catch a fork that quietly reclassified it, and a
  // projection is a place that could.
  EXPECT_EQ(project::Project(NavigationErrorClass::kBlockedBySafeBrowsing),
            mojom::NavigationFailure::kBlockedBySafeBrowsing);
  EXPECT_EQ(project::Project(NavigationErrorClass::kBlockedByClient),
            mojom::NavigationFailure::kBlockedByClient);
  EXPECT_EQ(project::Project(NavigationErrorClass::kUnknownFailure),
            mojom::NavigationFailure::kUnknownFailure);
}

TEST(BrowsingProjectionTest, AnUnrecognisedInterstitialStaysVisible) {
  EXPECT_EQ(project::Project(InterstitialKind::kNone),
            mojom::InterstitialKind::kNone);
  EXPECT_EQ(project::Project(InterstitialKind::kCertificateError),
            mojom::InterstitialKind::kCertificateError);
  // kOther must not flatten to kNone. That some trusted surface is standing in
  // front of the page is the part no surface may lose.
  EXPECT_EQ(project::Project(InterstitialKind::kOther),
            mojom::InterstitialKind::kOther);
  EXPECT_NE(project::Project(InterstitialKind::kOther),
            mojom::InterstitialKind::kNone);
}

TEST(BrowsingProjectionTest, ARefusalKeepsItsKind) {
  EXPECT_EQ(project::Project(TabOpenResult::kOpened),
            mojom::BrowsingStatus::kAccepted);
  EXPECT_EQ(project::Project(TabOpenResult::kRefusedAssistantOutsideTaskScope),
            mojom::BrowsingStatus::kRefusedByPolicy);
  EXPECT_EQ(project::Project(TabOpenResult::kRefusedNoDelegate),
            mojom::BrowsingStatus::kUnavailable);
  EXPECT_EQ(project::Project(TabActivateResult::kRefusedUnknownTab),
            mojom::BrowsingStatus::kUnknownTab);
  EXPECT_EQ(
      project::Project(TabCloseResult::kRefusedAssistantMayNotCloseUserTab),
      mojom::BrowsingStatus::kRefusedByPolicy);
}

TEST(BrowsingProjectionTest, AskingForAStateThatAlreadyHoldsIsAccepted) {
  // An idempotent command reported as a refusal is a control a surface has to
  // explain away, so selecting the tab that is already selected is ACCEPTED
  // rather than a refusal with no user-visible cause.
  EXPECT_EQ(project::Project(TabActivateResult::kAlreadyActive),
            mojom::BrowsingStatus::kAccepted);
  EXPECT_EQ(project::Project(TabActivateResult::kActivated),
            mojom::BrowsingStatus::kAccepted);
}

TEST(BrowsingProjectionTest, EveryDownloadRefusalIsTheSameKindOfRefusal) {
  EXPECT_EQ(project::Project(DownloadCommandLegality::kLegal),
            mojom::BrowsingStatus::kAccepted);
  // Which predicate refused is a browser fact. A surface that could tell them
  // apart would be carrying a copy of the state table.
  EXPECT_EQ(project::Project(DownloadCommandLegality::kIllegalInState),
            mojom::BrowsingStatus::kIllegalInState);
  EXPECT_EQ(project::Project(DownloadCommandLegality::kNotResumable),
            mojom::BrowsingStatus::kIllegalInState);
  EXPECT_EQ(project::Project(DownloadCommandLegality::kNeedsDangerConfirmation),
            mojom::BrowsingStatus::kIllegalInState);
}

TEST(BrowsingProjectionTest, AnUnknownDownloadSizeIsAFlagAndNotASentinel) {
  DownloadRecord record;
  record.download_id = 7u;
  record.target_file_name = "report (1).pdf";
  record.state = DownloadState::kInProgress;
  record.received_bytes = 4096;
  record.total_bytes = -1;

  mojom::DownloadViewPtr view =
      project::ProjectDownload(record, "files.example.test");
  ASSERT_TRUE(view);
  EXPECT_EQ(view->download_id, "7");
  EXPECT_EQ(view->file_name, "report (1).pdf");
  EXPECT_EQ(view->host, "files.example.test");
  EXPECT_EQ(view->received_bytes, 4096u);
  EXPECT_FALSE(view->total_known);
  // The -1 does not cross. A surface that received it as a size would draw a
  // bar at eighteen quintillion bytes rather than a spinner.
  EXPECT_EQ(view->total_bytes, 0u);
}

TEST(BrowsingProjectionTest, AKnownDownloadSizeCrossesAsItself) {
  DownloadRecord record;
  record.download_id = 8u;
  record.state = DownloadState::kComplete;
  record.destination = DownloadDestinationKind::kUserChosenLocation;
  record.received_bytes = 900;
  record.total_bytes = 900;
  record.requires_danger_confirmation = true;

  mojom::DownloadViewPtr view = project::ProjectDownload(record, "example.test");
  ASSERT_TRUE(view);
  EXPECT_TRUE(view->total_known);
  EXPECT_EQ(view->total_bytes, 900u);
  EXPECT_EQ(view->state, mojom::DownloadState::kComplete);
  EXPECT_EQ(view->destination, mojom::DownloadDestination::kUserChosenLocation);
  EXPECT_TRUE(view->requires_danger_confirmation);
}

TEST(BrowsingProjectionTest, ACommandThisBuildHasIsAccepted) {
  EXPECT_EQ(project::AcceptCommand(mojom::DownloadCommand::kPause),
            DownloadCommand::kPause);
  // Retry is a fresh request rather than a continuation, and the two must not
  // collapse on the way in: one continues an authorized transfer and the other
  // makes a new one.
  EXPECT_EQ(project::AcceptCommand(mojom::DownloadCommand::kRetry),
            DownloadCommand::kRetry);
  EXPECT_EQ(project::AcceptCommand(mojom::DownloadCommand::kResume),
            DownloadCommand::kResume);
  EXPECT_NE(project::AcceptCommand(mojom::DownloadCommand::kRetry),
            project::AcceptCommand(mojom::DownloadCommand::kResume));
}

// The generated decoders, against the same values the projection produces.
// This is the half that stops the two from drifting: a wire integer means the
// same thing to the decoder as the projection meant when it wrote it.
TEST(BrowsingProjectionTest, TheDecoderAgreesWithTheProjection) {
  EXPECT_EQ(wire::NavigationFailureFromWire(
                static_cast<uint32_t>(project::Project(
                    NavigationErrorClass::kBlockedBySafeBrowsing))),
            mojom::NavigationFailure::kBlockedBySafeBrowsing);
  EXPECT_EQ(wire::DownloadStateFromWire(
                static_cast<uint32_t>(project::Project(DownloadState::kPaused))),
            mojom::DownloadState::kPaused);
  EXPECT_EQ(wire::TabOwnerFromWire(static_cast<uint32_t>(
                project::Project(TabOwnership::kAssistant))),
            mojom::TabOwner::kAssistant);
  EXPECT_EQ(wire::BrowsingStatusFromWire(static_cast<uint32_t>(
                project::Project(TabCloseResult::kRefusedUnknownTab))),
            mojom::BrowsingStatus::kUnknownTab);
}

TEST(BrowsingProjectionTest, OnePastTheLastMemberIsRefused) {
  // One past the last declared member of each enumeration, which is what a
  // peer built against a later contract minor would send. Appending a member
  // turns this case red rather than quietly turning a refusal into an
  // acceptance.
  EXPECT_EQ(wire::TabOwnerFromWire(3u), std::nullopt);
  EXPECT_EQ(wire::TabPrivacyFromWire(2u), std::nullopt);
  EXPECT_EQ(wire::NavigationFailureFromWire(11u), std::nullopt);
  EXPECT_EQ(wire::InterstitialKindFromWire(5u), std::nullopt);
  EXPECT_EQ(wire::DownloadStateFromWire(6u), std::nullopt);
  EXPECT_EQ(wire::DownloadFailureFromWire(10u), std::nullopt);
  EXPECT_EQ(wire::DownloadDestinationFromWire(4u), std::nullopt);
  EXPECT_EQ(wire::DownloadCommandFromWire(6u), std::nullopt);
  EXPECT_EQ(wire::BrowsingStatusFromWire(8u), std::nullopt);
  EXPECT_EQ(wire::BrowsingStatusFromWire(0xFFFFFFFFu), std::nullopt);
}

TEST(BrowsingProjectionTest, TheDecoderIsUsableInAConstantExpression) {
  static_assert(wire::DownloadStateFromWire(4u) ==
                mojom::DownloadState::kComplete);
  static_assert(wire::DownloadStateFromWire(9u) == std::nullopt);
  SUCCEED();
}

// The one rule about a snapshot the contract's per-record shape rules cannot
// express, which is why compat/manifest.json carries it as an `unexecuted`
// row and points here.
mojom::TabViewPtr Tab(const std::string& id,
                      const std::string& host,
                      bool selected) {
  auto tab = mojom::TabView::New();
  tab->tab_id = id;
  tab->host = host;
  tab->owner = mojom::TabOwner::kUser;
  tab->privacy = mojom::TabPrivacy::kNormal;
  tab->selected = selected;
  return tab;
}

mojom::BrowsingStateViewPtr StateShowing(const std::string& host) {
  auto state = mojom::BrowsingStateView::New();
  state->tabs.push_back(Tab("tab-1", "example.test", host == "example.test"));
  state->tabs.push_back(Tab("tab-2", "other.test", host == "other.test"));
  state->navigation = mojom::NavigationView::New();
  state->navigation->host = host;
  return state;
}

TEST(BrowsingProjectionTest, ASnapshotDescribesOneBrowser) {
  EXPECT_TRUE(project::StateIsCoherent(*StateShowing("example.test")));
  EXPECT_TRUE(project::StateIsCoherent(*StateShowing("other.test")));
}

TEST(BrowsingProjectionTest, ASnapshotWhoseHalvesDisagreeIsRefused) {
  // Well-formed records, incoherent together: the address bar would name a
  // page no tab in the same snapshot is on.
  mojom::BrowsingStateViewPtr disagreeing = StateShowing("example.test");
  disagreeing->navigation->host = "somewhere.else.test";
  EXPECT_FALSE(project::StateIsCoherent(*disagreeing));

  mojom::BrowsingStateViewPtr none = StateShowing("example.test");
  none->tabs[0]->selected = false;
  EXPECT_FALSE(project::StateIsCoherent(*none));

  mojom::BrowsingStateViewPtr two = StateShowing("example.test");
  two->tabs[1]->selected = true;
  EXPECT_FALSE(project::StateIsCoherent(*two));
}

TEST(BrowsingProjectionTest, ATabThatHasBeenNowhereIsStillCoherent) {
  // Both halves carry no host, and that is the pair agreeing rather than the
  // pair being empty: a new tab has been nowhere and the address bar shows
  // nothing, which is one browser and not two.
  mojom::BrowsingStateViewPtr fresh = StateShowing("");
  fresh->tabs[0]->host = "";
  fresh->tabs[0]->has_been_nowhere = true;
  fresh->tabs[0]->selected = true;
  EXPECT_TRUE(project::StateIsCoherent(*fresh));
}

}  // namespace
}  // namespace taffy
