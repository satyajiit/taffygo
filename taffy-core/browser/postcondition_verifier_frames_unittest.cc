// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/components/intelligence/content/postcondition_evidence.h"
#include "taffy/components/intelligence/content/postcondition_verifier.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

// Decision 0230: a navigation the action did not start - a frame inside the
// page committing - is not evidence about the action, in either direction.
class PostconditionVerifierFramesTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://results.example/search"));
  }

  // A followed link, declared to land at `origin`.
  void StartVerifying(const char* origin) {
    Postcondition committed;
    committed.kind = PostconditionKind::kCommittedNavigation;
    committed.allowed_origins.push_back(
        Origin{.kind = OriginKind::kTuple, .serialization = origin});
    std::vector<Postcondition> postconditions;
    postconditions.push_back(std::move(committed));
    verifier_.emplace(web_contents(), nullptr, base::DoNothing(), nullptr);
    verifier_->Start(NodeHandle{}, std::move(postconditions),
                     /*dispatch_revision=*/1u, DispatchWatermark{},
                     IdempotencyPolicy::kPureRead, base::Seconds(10),
                     base::BindLambdaForTesting(
                         [this](PostconditionVerifier::Outcome outcome) {
                           outcome_ = std::move(outcome);
                         }));
  }

  void CommitInAFrame(const GURL& url) {
    content::RenderFrameHost* frame =
        content::RenderFrameHostTester::For(main_rfh())->AppendChild("frame");
    content::NavigationSimulator::NavigateAndCommitFromDocument(url, frame);
  }

  std::optional<PostconditionVerifier> verifier_;
  std::optional<PostconditionVerifier::Outcome> outcome_;
};

TEST_F(PostconditionVerifierFramesTest, AFrameCommittingElsewhereContradictsNothing) {
  StartVerifying("https://destination.example");
  CommitInAFrame(GURL("https://frames.example/ad"));
  EXPECT_FALSE(outcome_.has_value());

  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://destination.example/landing"));
  ASSERT_TRUE(outcome_.has_value());
  EXPECT_EQ(outcome_->code, ActionResultCode::kVerified);
}

TEST_F(PostconditionVerifierFramesTest, AFrameCommittingAtTheDestinationVerifiesNothing) {
  StartVerifying("https://destination.example");
  CommitInAFrame(GURL("https://destination.example/embedded"));
  EXPECT_FALSE(outcome_.has_value());
}

}  // namespace
}  // namespace taffy
