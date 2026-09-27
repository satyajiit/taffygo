// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/services/core/rust_core.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kNow = 10'000u;

mojom::CoreBootstrapPtr BootstrapForParallelReads() {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = kGeneration;
  bootstrap->generation_capability_entropy.assign(32u, 1u);
  bootstrap->generation_capability_entropy.back() = 3u;
  bootstrap->browser_profile_id = "profile-parallel";
  bootstrap->browser_session_id = "session-parallel";
  return bootstrap;
}

mojom::CoreServiceCommandPtr StartComparison() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "start-parallel", kGeneration, 0u, kNow + 60'000u, "start-parallel-key");
  command->kind = mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = mojom::StartTaskCommand::New();
  auto& start = *command->start_task;
  start.task_id = "task-parallel";
  start.browser_profile_id = "profile-parallel";
  start.browser_session_id = "session-parallel";
  start.kind = mojom::TaskKind::kResearch;
  start.goal = "Compare the two selected pages";
  start.control_mode = mojom::TaskControlMode::kAssistant;
  start.provider_route_id = "direct_user_key";
  start.assistant_config_version = 1u;
  start.policy_version = 1u;
  start.tool_allowlist = {"browser.dom.read"};
  start.milestone = mojom::TaskMilestone::kM7;
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxSources, 2u));
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxModelRequests, 32u));
  // A start that may call a model states how often one turn may be paid for
  // again; `MAX_RETRIES_PER_STEP` in the start-task decoder is the other half
  // of this number (decision 0218).
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxRetriesPerStep, 2u));
  for (auto kind : {mojom::TaskBudgetKind::kMaxInputUnits,
                    mojom::TaskBudgetKind::kMaxOutputUnits,
                    mojom::TaskBudgetKind::kMaxCostUnits}) {
    start.budgets.push_back(mojom::TaskBudget::New(kind, 0u));
  }
  start.trace_id = "trace-parallel";
  start.task_id_seed.assign(32u, 2u);
  start.task_id_seed.back() = 4u;
  start.template_id = mojom::TaskTemplateId::kCompareProducts;
  start.consent_preview = mojom::TaskConsentPreview::New();
  start.consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("00112233445566778899aabbccddeeff", "tab-a",
                                    "https://first.test", std::nullopt));
  start.consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("10112233445566778899aabbccddeeff", "tab-b",
                                    "https://second.test", std::nullopt));
  start.consent_preview->provider_route =
      mojom::TaskProviderRoute::kDirectUserKey;
  start.initial_consent_receipt_id = "consent-parallel";
  return command;
}

// Runs the real Rust bridge and journal continuation, retaining every effect
// published beside an intermediate durable state. No page or model executes.
bool CommitUntilWaiting(RustCore& core,
                        CoreResponseBatch batch,
                        uint64_t& last_sequence,
                        std::vector<mojom::TaskEffectBindingPtr>& effects) {
  for (uint64_t step = 0u; step < 32u; ++step) {
    if (!batch.admission ||
        batch.admission->status != mojom::AdmissionStatus::kAccepted) {
      ADD_FAILURE() << "bridge refused continuation at step " << step;
      return false;
    }
    for (auto& state : batch.states) {
      if (!state.state || !state.browser_bindings ||
          state.state->sequence != last_sequence + 1u ||
          state.browser_bindings->state_sequence != state.state->sequence) {
        ADD_FAILURE() << "durable publication skipped sequence "
                      << last_sequence + 1u << " at continuation " << step;
        return false;
      }
      last_sequence = state.state->sequence;
      for (auto& effect : state.task_effects) {
        effects.push_back(std::move(effect));
      }
    }
    if (batch.effects.empty()) {
      return true;
    }
    if (batch.effects.size() != 1u || !batch.effects.front() ||
        !batch.effects.front()->storage_commit) {
      ADD_FAILURE() << "expected one exact storage commit";
      return false;
    }
    auto& effect = *batch.effects.front();
    auto result = mojom::EffectResult::New();
    result->operation = effect.operation.Clone();
    result->effect_id = effect.effect_id;
    result->status = mojom::EffectStatus::kCompleted;
    result->kind = mojom::EffectKind::kStorageCommit;
    result->storage = mojom::StorageEffectResult::New(
        effect.storage_commit->resulting_revision);
    batch = core.DeliverEffectResult(std::move(result), kNow + step + 1u);
  }
  ADD_FAILURE() << "continuation did not reach its browser wait";
  return false;
}

// The browser is simulated here: this tests transport after policy has
// answered, not the policy decision itself or a live capability spend.
mojom::PolicyEvaluationResultPtr GrantRead(
    const mojom::TaskEffectBinding& effect,
    const std::string& origin) {
  auto result = mojom::PolicyEvaluationResult::New();
  result->operation_id = effect.operation->operation_id;
  result->status = mojom::PolicyEvaluationStatus::kGranted;
  result->minted_grant = mojom::MintedCapabilityGrant::New();
  auto& grant = *result->minted_grant;
  grant.capability_id = "cap-" + effect.policy->action_id;
  grant.scope = mojom::PolicyCapabilityScope::New();
  grant.scope->frame_id = "frame-" + effect.policy->tab_id;
  grant.scope->page_epoch = "epoch-" + effect.policy->tab_id;
  grant.scope->required_graph_revision = 1u;
  grant.scope->origin = mojom::PolicyOrigin::New();
  grant.scope->origin->kind = mojom::PolicyOriginKind::kTuple;
  grant.scope->origin->serialization = origin;
  return result;
}

TEST(RustCoreParallelObservationTest,
     ACommittedReadPublishesBeforeTheNextSourcesPolicyWithoutAReadTerminal) {
  RustCore core;
  auto initialized = core.Initialize(BootstrapForParallelReads());
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);
  ASSERT_EQ(1u, initialized.states.size());
  ASSERT_TRUE(initialized.states.front().state);
  uint64_t last_sequence = initialized.states.front().state->sequence;

  std::vector<mojom::TaskEffectBindingPtr> effects;
  ASSERT_TRUE(CommitUntilWaiting(core, core.Submit(StartComparison(), kNow),
                                last_sequence, effects));
  ASSERT_EQ(1u, effects.size());
  ASSERT_EQ(mojom::TaskReducerEffectKind::kAskPolicy, effects.front()->kind);
  auto first_policy = std::move(effects.front());
  effects.clear();

  ASSERT_TRUE(CommitUntilWaiting(
      core,
      core.CompleteTaskPolicy(first_policy.Clone(),
                              GrantRead(*first_policy, "https://first.test"),
                              kNow + 50u),
      last_sequence, effects));
  ASSERT_EQ(2u, effects.size());
  ASSERT_EQ(mojom::TaskReducerEffectKind::kDispatchAction, effects[0]->kind);
  ASSERT_TRUE(effects[0]->action);
  EXPECT_EQ("tab-a", effects[0]->action->executable->tab_id);
  ASSERT_EQ(mojom::TaskReducerEffectKind::kAskPolicy, effects[1]->kind);
  ASSERT_TRUE(effects[1]->policy);
  EXPECT_EQ("tab-b", effects[1]->policy->tab_id);
  EXPECT_LT(effects[0]->operation->task_revision,
            effects[1]->operation->task_revision);

  auto second_policy = std::move(effects[1]);
  effects.clear();
  ASSERT_TRUE(CommitUntilWaiting(
      core,
      core.CompleteTaskPolicy(second_policy.Clone(),
                              GrantRead(*second_policy, "https://second.test"),
                              kNow + 100u),
      last_sequence, effects));
  ASSERT_EQ(1u, effects.size());
  EXPECT_EQ(mojom::TaskReducerEffectKind::kDispatchAction,
            effects.front()->kind);
  ASSERT_TRUE(effects.front()->action);
  EXPECT_EQ("tab-b", effects.front()->action->executable->tab_id);
  // Neither read has returned, so there must be no model effect yet. The last
  // read has no following source to propose: its continuation must publish
  // the already-created durable snapshot, without consuming another sequence.
}

}  // namespace
}  // namespace taffy
