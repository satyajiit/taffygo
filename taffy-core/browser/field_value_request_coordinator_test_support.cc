// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/field_value_request_coordinator_test_support.h"

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"

namespace taffy::field_value_request_coordinator_test {

ResolvedNodeFacts EditableField(std::string node_id, Sensitivity sensitivity) {
  ResolvedNodeFacts facts;
  facts.node_id = SemanticNodeId{std::move(node_id)};
  facts.display_label = facts.node_id.value;
  facts.observed_at_revision = 12u;
  facts.available_actions = {ActionType::kSetText};
  facts.asserted_states = {NodeState::kVisible, NodeState::kEnabled,
                           NodeState::kEditable};
  facts.sensitivity = sensitivity;
  return facts;
}

ResolvedNodeFacts EditableField(Sensitivity sensitivity) {
  return EditableField(kNodeId, sensitivity);
}

RecordingClient::RecordingClient() = default;
RecordingClient::~RecordingClient() = default;

void RecordingClient::Open(surface_mojom::FieldValueRequestPtr request) {
  opened.push_back(std::move(request));
}

void RecordingClient::Close(const std::string& request_id,
                            surface_mojom::FieldValueCloseReason reason) {
  closed.emplace_back(request_id, reason);
}

mojo::PendingRemote<surface_mojom::TaffyFieldValueClient>
RecordingClient::Bind() {
  return receiver.BindNewPipeAndPassRemote();
}

void FieldValueRequestCoordinatorTest::SetUp() {
  vault_.BeginGeneration("profile_1", 1u);
  Build();
}

void FieldValueRequestCoordinatorTest::Build() {
  coordinator_ = std::make_unique<FieldValueRequestCoordinator>(
      &vault_,
      base::BindLambdaForTesting(
          [this](const std::string& tab_id, const std::string& node_id,
                 FieldValueRequestCoordinator::NodeFactsCallback resolved) {
            ++resolves_;
            EXPECT_EQ(kTabId, tab_id);
            const auto configured = node_facts_.find(node_id);
            std::move(resolved).Run(
                node_gone_ ? std::optional<ResolvedNodeFacts>()
                : configured != node_facts_.end()
                    ? std::optional<ResolvedNodeFacts>(configured->second)
                    : std::optional<ResolvedNodeFacts>([&] {
                        EXPECT_EQ(kNodeId, node_id);
                        ResolvedNodeFacts facts = EditableField(sensitivity_);
                        facts.challenge_kind = challenge_kind_;
                        facts.challenge_bounds = challenge_bounds_;
                        return facts;
                      }()));
          }),
      base::BindLambdaForTesting(
          [this](const std::string&) {
            return std::make_optional(FieldValueDocument{
                .host = "bank.test",
                .normalized_origin = "https://bank.test",
                .frame_id = "frame-1",
                .page_epoch = page_epoch_,
                .graph_revision = 12u,
            });
          }),
      base::BindLambdaForTesting(
          [this](const std::string& task_id, const std::string& request_id,
                 uint32_t supplied,
                 core_service::mojom::FieldValueAskOutcome outcome,
                 const std::vector<std::string>& field_node_ids) {
            reported_.emplace_back(request_id, supplied);
            reported_outcomes_.emplace_back(request_id, outcome);
            reported_field_node_ids_.emplace_back(request_id, field_node_ids);
            events_.push_back("reported " + request_id);
          }),
      base::BindLambdaForTesting(
          [this](const std::string& tab_id, const std::string& node_id,
                 ResolvedNodeFacts facts,
                 FieldChallengePresentationCallback completion)
              -> base::OnceClosure {
            ++challenge_presentations_;
            EXPECT_EQ(kTabId, tab_id);
            EXPECT_EQ(facts.node_id.value, node_id);
            if (hold_challenge_completion_) {
              pending_challenge_completion_ = std::move(completion);
              return base::BindOnce(
                  [](FieldValueRequestCoordinatorTest* self) {
                    self->challenge_cancelled_ = true;
                    if (self->pending_challenge_completion_) {
                      std::move(self->pending_challenge_completion_)
                          .Run(std::nullopt, "cancelled");
                    }
                  },
                  base::Unretained(this));
            }
            std::optional<FieldChallengePresentation> presentation;
            if (challenge_presentation_available_) {
              presentation.emplace();
              if (facts.challenge_kind == ChallengeKind::kImage) {
                presentation->image_png = {0x89u, 0x50u, 0x4eu, 0x47u};
              } else if (facts.challenge_kind == ChallengeKind::kInteractive) {
                presentation->highlight = NormalizedFieldHighlight{
                    .left = 0.1f,
                    .top = 0.2f,
                    .right = 0.4f,
                    .bottom = 0.5f,
                };
              }
            }
            // The clause is what the coordinator folds into an outcome, so
            // a fixture that refuses has to name one (decision 0215).
            const char* refused_at =
                presentation.has_value() ? nullptr : challenge_refusal_;
            std::move(completion).Run(std::move(presentation), refused_at);
            return base::OnceClosure(base::DoNothing());
          }),
      base::BindLambdaForTesting([this](const std::string& request_id) {
        closed_request_ids_.push_back(request_id);
        events_.push_back("closed " + request_id);
      }));
  coordinator_->Connect(client_.Bind());
}

void FieldValueRequestCoordinatorTest::OpenRequest() {
  coordinator_->OnCoreFieldValueRequest(kRequestId, kTaskId, kTabId, kNodeId);
}

SupplyOutcome FieldValueRequestCoordinatorTest::Supply(
    std::vector<std::string> values) {
  SupplyOutcome outcome;
  coordinator_->Supply(
      kRequestId, values,
      base::BindLambdaForTesting(
          [&outcome](surface_mojom::FieldValueSupplyVerdict verdict,
                     std::vector<surface_mojom::FieldValueRefusalPtr> refused) {
            outcome.answered = true;
            outcome.verdict = verdict;
            outcome.refused = std::move(refused);
          }));
  return outcome;
}

}  // namespace taffy::field_value_request_coordinator_test
