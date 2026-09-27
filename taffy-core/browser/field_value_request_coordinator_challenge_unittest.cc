// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "content/public/test/browser_task_environment.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace surface_mojom = browser::field_values::mojom;
namespace core_mojom = core_service::mojom;

constexpr char kRequestId[] = "turn-4-values-1";
constexpr char kTaskId[] = "task-1";
constexpr char kTabId[] = "tab-1";
constexpr char kNodeId[] = "node-7";

class ChallengeClient final : public surface_mojom::TaffyFieldValueClient {
 public:
  void Open(surface_mojom::FieldValueRequestPtr request) override {
    opened.push_back(std::move(request));
  }
  void Close(const std::string&,
             surface_mojom::FieldValueCloseReason) override {}
  mojo::PendingRemote<surface_mojom::TaffyFieldValueClient> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  std::vector<surface_mojom::FieldValueRequestPtr> opened;

 private:
  mojo::Receiver<surface_mojom::TaffyFieldValueClient> receiver_{this};
};

class FieldValueChallengeTest : public testing::Test {
 protected:
  void SetUp() override {
    vault_.BeginGeneration("profile_1", 1u);
    coordinator_ = std::make_unique<FieldValueRequestCoordinator>(
        &vault_,
        base::BindLambdaForTesting(
            [this](const std::string&, const std::string& node_id,
                   FieldValueRequestCoordinator::NodeFactsCallback callback) {
              ResolvedNodeFacts facts;
              facts.node_id = SemanticNodeId{node_id};
              facts.display_label = "Verification code";
              facts.observed_at_revision = 12u;
              facts.available_actions = {ActionType::kSetText};
              facts.asserted_states = {NodeState::kVisible, NodeState::kEnabled,
                                       NodeState::kEditable};
              facts.sensitivity = Sensitivity::kChallengeResponse;
              facts.challenge_kind = challenge_kind_;
              facts.challenge_bounds = ChallengeBounds{10, 20, 120, 80};
              std::move(callback).Run(std::move(facts));
            }),
        base::BindRepeating(
            [](const std::string&) {
              return std::make_optional(FieldValueDocument{
                  .host = "bank.test",
                  .normalized_origin = "https://bank.test",
                  .frame_id = "frame-1",
                  .page_epoch = "page-1",
                  .graph_revision = 12u,
              });
            }),
        base::BindLambdaForTesting(
            [this](const std::string&, const std::string&, uint32_t supplied,
                   core_mojom::FieldValueAskOutcome outcome,
                   const std::vector<std::string>&) {
              reported_.push_back(supplied);
              reported_outcomes_.push_back(outcome);
            }),
        base::BindLambdaForTesting(
            [this](const std::string&, const std::string&,
                   ResolvedNodeFacts facts,
                   FieldChallengePresentationCallback completion)
                -> base::OnceClosure {
              ++presentations_;
              if (hold_completion_) {
                pending_completion_ = std::move(completion);
                return base::BindOnce(
                    [](FieldValueChallengeTest* self) {
                      self->cancelled_ = true;
                      if (self->pending_completion_) {
                        std::move(self->pending_completion_)
                            .Run(std::nullopt, "cancelled");
                      }
                    },
                    base::Unretained(this));
              }
              std::optional<FieldChallengePresentation> presentation;
              if (presentation_available_) {
                presentation.emplace();
                if (facts.challenge_kind == ChallengeKind::kImage) {
                  presentation->image_png = {0x89u, 0x50u, 0x4eu, 0x47u};
                } else {
                  presentation->highlight = {
                      .left = 0.1f, .top = 0.2f, .right = 0.4f, .bottom = 0.5f};
                }
              }
              std::move(completion).Run(
                  std::move(presentation),
                  presentation_available_ ? nullptr : refusal_);
              return base::OnceClosure(base::DoNothing());
            }));
    coordinator_->Connect(client_.Bind());
  }

  void Open(ChallengeKind kind) {
    challenge_kind_ = kind;
    coordinator_->OnCoreFieldValueRequest(kRequestId, kTaskId, kTabId, kNodeId);
    task_environment_.RunUntilIdle();
  }

  surface_mojom::FieldValueSupplyVerdict Supply(std::string value) {
    surface_mojom::FieldValueSupplyVerdict verdict =
        surface_mojom::FieldValueSupplyVerdict::kMalformed;
    coordinator_->Supply(
        kRequestId, {std::move(value)},
        base::BindLambdaForTesting(
            [&verdict](surface_mojom::FieldValueSupplyVerdict result,
                       std::vector<surface_mojom::FieldValueRefusalPtr>) {
              verdict = result;
            }));
    return verdict;
  }

  content::BrowserTaskEnvironment task_environment_;
  ValueReferenceVault vault_;
  ChallengeClient client_;
  ChallengeKind challenge_kind_ = ChallengeKind::kNone;
  bool presentation_available_ = true;
  // What the fake capture refuses under. The coordinator reads this clause and
  // not its own to tell "the picture is below the fold" from "there is no
  // picture at all" (decision 0215).
  const char* refusal_ = "no-surface-bitmap";
  bool hold_completion_ = false;
  bool cancelled_ = false;
  int presentations_ = 0;
  FieldChallengePresentationCallback pending_completion_;
  std::vector<uint32_t> reported_;
  std::vector<core_mojom::FieldValueAskOutcome> reported_outcomes_;
  std::unique_ptr<FieldValueRequestCoordinator> coordinator_;
};

TEST_F(FieldValueChallengeTest, ImageCarriesOnlyBrowserPresentationBytes) {
  Open(ChallengeKind::kImage);

  ASSERT_EQ(1, presentations_);
  ASSERT_EQ(1u, client_.opened.size());
  const auto& field = client_.opened[0]->fields[0];
  EXPECT_EQ(surface_mojom::FieldChallengeKind::kImageChallenge,
            field->challenge);
  ASSERT_TRUE(field->challenge_image.has_value());
  EXPECT_EQ((std::vector<uint8_t>{0x89u, 0x50u, 0x4eu, 0x47u}),
            *field->challenge_image);
  EXPECT_FALSE(field->highlight);
  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply("bluebird"));

  const ValueReference reference{DerivedValueReference(kRequestId, 0u)};
  std::string value;
  EXPECT_EQ(ValueResolution::kResolved,
            vault_.Resolve(reference, TaskId{kTaskId},
                           Sensitivity::kChallengeResponse,
                           base::TimeTicks::Now(), value));
  EXPECT_EQ("bluebird", value);
}

TEST_F(FieldValueChallengeTest, InteractiveCarriesHighlightAndNeverPixels) {
  Open(ChallengeKind::kInteractive);
  ASSERT_EQ(1u, client_.opened.size());
  const auto& field = client_.opened[0]->fields[0];
  EXPECT_FALSE(field->challenge_image.has_value());
  ASSERT_TRUE(field->highlight);
  EXPECT_FLOAT_EQ(0.1f, field->highlight->left);
  EXPECT_FLOAT_EQ(0.5f, field->highlight->bottom);
}

TEST_F(FieldValueChallengeTest, MissingPresentationFallsThroughToHandover) {
  presentation_available_ = false;
  Open(ChallengeKind::kImage);
  EXPECT_TRUE(client_.opened.empty());
  EXPECT_EQ(0u, coordinator_->open_request_count());
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(0u, reported_[0]);
  ASSERT_EQ(1u, reported_outcomes_.size());
  EXPECT_EQ(core_mojom::FieldValueAskOutcome::kCannotBeShown,
            reported_outcomes_[0]);
}

// The myAadhaar run, at the seam where it went wrong (decision 0215).
//
// The capture refuses under `bounds-outside-viewport`, the coordinator's own
// clause is still `no-challenge-picture`, and the count is still zero — all
// three of which were already true on the phone. What is new is that the task
// is told the target was right and the page has to move, instead of being
// handed a zero it can only answer by asking again.
TEST_F(FieldValueChallengeTest, AChallengeBelowTheFoldIsReportedAsOffScreen) {
  presentation_available_ = false;
  refusal_ = kFieldChallengeRefusedOffScreen;
  Open(ChallengeKind::kImage);

  EXPECT_TRUE(client_.opened.empty());
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(0u, reported_[0]);
  ASSERT_EQ(1u, reported_outcomes_.size());
  EXPECT_EQ(core_mojom::FieldValueAskOutcome::kChallengeOffScreen,
            reported_outcomes_[0]);
}

// The distinction is read off the capture's clause and nowhere else, so a
// refusal that is not about geometry must not borrow the scroll instruction.
TEST_F(FieldValueChallengeTest, AnyOtherCaptureRefusalIsNotOffScreen) {
  presentation_available_ = false;
  for (const char* clause : {"no-challenge-bounds", "no-viewport",
                             "no-surface-bitmap", "deadline"}) {
    reported_.clear();
    reported_outcomes_.clear();
    refusal_ = clause;
    coordinator_->CloseRequestsForTask(kTaskId);
    Open(ChallengeKind::kImage);

    ASSERT_EQ(1u, reported_outcomes_.size()) << clause;
    EXPECT_EQ(core_mojom::FieldValueAskOutcome::kCannotBeShown,
              reported_outcomes_[0])
        << clause << " borrowed the scroll instruction";
  }
}

// An interactive challenge is the same geometry failure wearing another kind:
// the outline is placed from the same clipped rectangle as the picture.
TEST_F(FieldValueChallengeTest, AnInteractiveChallengeOffScreenSaysSo) {
  presentation_available_ = false;
  refusal_ = kFieldChallengeRefusedOffScreen;
  Open(ChallengeKind::kInteractive);

  ASSERT_EQ(1u, reported_outcomes_.size());
  EXPECT_EQ(core_mojom::FieldValueAskOutcome::kChallengeOffScreen,
            reported_outcomes_[0]);
}

TEST_F(FieldValueChallengeTest, ClosingPendingCaptureCancelsIt) {
  hold_completion_ = true;
  Open(ChallengeKind::kImage);
  ASSERT_TRUE(pending_completion_);
  coordinator_->CloseRequestsForTask(kTaskId);
  EXPECT_TRUE(cancelled_);
  EXPECT_FALSE(pending_completion_);
  EXPECT_EQ(0u, coordinator_->open_request_count());
  EXPECT_TRUE(client_.opened.empty());
}

}  // namespace
}  // namespace taffy
