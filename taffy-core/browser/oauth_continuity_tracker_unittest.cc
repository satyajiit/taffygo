// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/oauth_continuity_tracker.h"

#include "base/test/simple_test_tick_clock.h"
#include "taffy/browser/credential_boundary.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

// PAR-AUTH-004: the state survives the external provider flow, and nothing
// about the callback reaches the assistant.

namespace taffy {
namespace {

class OAuthContinuityTrackerTest : public testing::Test {
 protected:
  void SetUp() override {
    tracker_ = std::make_unique<OAuthContinuityTracker>(&boundary_);
    tracker_->SetTickClockForTesting(&clock_);
    clock_.SetNowTicks(base::TimeTicks::Now());
  }

  void TearDown() override {
    tracker_.reset();
    OriginCodec::Get().ClearForTesting();
  }

  Origin WireOrigin(const char* url) {
    return OriginCodec::Get().ToWireOrigin(url::Origin::Create(GURL(url)));
  }

  OAuthHandoffRequest UserRequest() {
    OAuthHandoffRequest request;
    request.tab_id = TabId{"tab_oauth"};
    request.initiating_origin = WireOrigin("https://primary.taffy.test/");
    request.expected_callback_origin = WireOrigin("https://primary.taffy.test/");
    request.expected_callback_path = "/auth/callback";
    request.initiator = NavigationInitiator::kUser;
    request.has_user_activation = true;
    return request;
  }

  OAuthReturnFacts Callback(const char* url) {
    return OAuthReturnFacts::FromCallbackUrl(GURL(url), TabId{"tab_oauth"});
  }

  content::BrowserTaskEnvironment task_environment_;
  base::SimpleTestTickClock clock_;
  CredentialBoundary boundary_;
  std::unique_ptr<OAuthContinuityTracker> tracker_;
};

TEST_F(OAuthContinuityTrackerTest, TheOrdinaryRoundTripResumesTheTab) {
  ASSERT_EQ(OAuthHandoffResult::kRecorded,
            tracker_->BeginAppSwitch(UserRequest()));
  EXPECT_TRUE(tracker_->HasPendingHandoff(TabId{"tab_oauth"}));

  // While the switch is outstanding the assistant cannot see the tab.
  EXPECT_EQ(AssistantAccess::kSuspendedAuthorizationHandoff,
            boundary_.EvaluateAssistantAccess(TabId{"tab_oauth"}));

  clock_.Advance(base::Seconds(45));
  EXPECT_EQ(OAuthReturnDecision::kResumeWaitingTab,
            tracker_->CompleteAppSwitch(Callback(
                "https://primary.taffy.test/auth/callback?code=VALUE&state=S")));

  EXPECT_FALSE(tracker_->HasPendingHandoff(TabId{"tab_oauth"}));
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(TabId{"tab_oauth"}));
}

TEST_F(OAuthContinuityTrackerTest, TheAssistantCannotStartASignInFlow) {
  OAuthHandoffRequest request = UserRequest();
  request.initiator = NavigationInitiator::kAssistant;

  EXPECT_EQ(OAuthHandoffResult::kRefusedAssistantInitiated,
            tracker_->BeginAppSwitch(request));
  EXPECT_FALSE(tracker_->HasPendingHandoff(TabId{"tab_oauth"}));
}

TEST_F(OAuthContinuityTrackerTest, AnUnattributedInitiatorIsRefused) {
  OAuthHandoffRequest request = UserRequest();
  request.initiator = NavigationInitiator::kUnknown;

  EXPECT_EQ(OAuthHandoffResult::kRefusedUnattributedInitiator,
            tracker_->BeginAppSwitch(request));
}

TEST_F(OAuthContinuityTrackerTest, APageRedirectWithoutAGestureIsRefused) {
  // Every real authorization flow starts with somebody tapping "sign in". One
  // that did not is a page redirecting into a provider on its own, and
  // resuming a tab for it later is automation, not continuity.
  OAuthHandoffRequest request = UserRequest();
  request.initiator = NavigationInitiator::kPage;
  request.has_user_activation = false;

  EXPECT_EQ(OAuthHandoffResult::kRefusedNoUserActivation,
            tracker_->BeginAppSwitch(request));
}

TEST_F(OAuthContinuityTrackerTest, TwoHandoffsInOneTabAreRefused) {
  ASSERT_EQ(OAuthHandoffResult::kRecorded,
            tracker_->BeginAppSwitch(UserRequest()));

  // A callback could not be attributed between them.
  EXPECT_EQ(OAuthHandoffResult::kRefusedAlreadyPending,
            tracker_->BeginAppSwitch(UserRequest()));
  EXPECT_EQ(1u, tracker_->pending_count());
}

TEST_F(OAuthContinuityTrackerTest, ACallbackAtAnotherOriginIsRefused) {
  ASSERT_EQ(OAuthHandoffResult::kRecorded,
            tracker_->BeginAppSwitch(UserRequest()));

  EXPECT_EQ(OAuthReturnDecision::kRefusedOriginMismatch,
            tracker_->CompleteAppSwitch(Callback(
                "https://hostile.taffy.test/auth/callback?code=VALUE")));

  // The handoff is cleared and access resumes: leaving it pending would let a
  // second, correct callback resume a tab whose flow was already interfered
  // with.
  EXPECT_FALSE(tracker_->HasPendingHandoff(TabId{"tab_oauth"}));
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(TabId{"tab_oauth"}));
}

TEST_F(OAuthContinuityTrackerTest, ACallbackAtAnotherPathIsRefused) {
  ASSERT_EQ(OAuthHandoffResult::kRecorded,
            tracker_->BeginAppSwitch(UserRequest()));

  EXPECT_EQ(OAuthReturnDecision::kRefusedPathMismatch,
            tracker_->CompleteAppSwitch(
                Callback("https://primary.taffy.test/somewhere-else?code=V")));
}

TEST_F(OAuthContinuityTrackerTest, AnyPathMatchesWhenNoneWasRegistered) {
  OAuthHandoffRequest request = UserRequest();
  request.expected_callback_path.clear();
  ASSERT_EQ(OAuthHandoffResult::kRecorded, tracker_->BeginAppSwitch(request));

  EXPECT_EQ(OAuthReturnDecision::kResumeWaitingTab,
            tracker_->CompleteAppSwitch(
                Callback("https://primary.taffy.test/anything?code=V")));
}

TEST_F(OAuthContinuityTrackerTest, AnExpiredHandoffDoesNotResume) {
  ASSERT_EQ(OAuthHandoffResult::kRecorded,
            tracker_->BeginAppSwitch(UserRequest()));

  clock_.Advance(OAuthContinuityTracker::HandoffWindow() + base::Seconds(1));

  EXPECT_EQ(OAuthReturnDecision::kRefusedExpired,
            tracker_->CompleteAppSwitch(
                Callback("https://primary.taffy.test/auth/callback?code=V")));
  // The tab is still there; the user can start again.
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(TabId{"tab_oauth"}));
}

TEST_F(OAuthContinuityTrackerTest, ASlowButLegitimateSwitchStillResumes) {
  // Installing a provider application, unlocking a password manager and
  // passing a biometric prompt is minutes, not seconds. The window has to be
  // long enough for the slow honest case.
  ASSERT_EQ(OAuthHandoffResult::kRecorded,
            tracker_->BeginAppSwitch(UserRequest()));
  clock_.Advance(OAuthContinuityTracker::HandoffWindow() - base::Seconds(1));

  EXPECT_EQ(OAuthReturnDecision::kResumeWaitingTab,
            tracker_->CompleteAppSwitch(
                Callback("https://primary.taffy.test/auth/callback?code=V")));
}

TEST_F(OAuthContinuityTrackerTest, ACallbackWithNoHandoffIsRefused) {
  EXPECT_EQ(OAuthReturnDecision::kRefusedNoPendingHandoff,
            tracker_->CompleteAppSwitch(
                Callback("https://primary.taffy.test/auth/callback?code=V")));
}

TEST_F(OAuthContinuityTrackerTest, CancellingResumesAssistantAccess) {
  ASSERT_EQ(OAuthHandoffResult::kRecorded,
            tracker_->BeginAppSwitch(UserRequest()));

  tracker_->CancelHandoff(TabId{"tab_oauth"});

  EXPECT_FALSE(tracker_->HasPendingHandoff(TabId{"tab_oauth"}));
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(TabId{"tab_oauth"}));
}

}  // namespace
}  // namespace taffy
