// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "taffy/browser/core_state_binding_registry.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kNowMillis = 10'000u;
constexpr char kTaskId[] = "task-pause-resume";

mojom::CoreBootstrapPtr ValidBootstrap() {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = kGeneration;
  bootstrap->generation_capability_entropy.resize(32u);
  for (size_t index = 0u;
       index < bootstrap->generation_capability_entropy.size(); ++index) {
    bootstrap->generation_capability_entropy[index] =
        static_cast<uint8_t>(index + 1u);
  }
  bootstrap->browser_profile_id = "profile-1";
  bootstrap->browser_session_id = "browser-session-1";
  return bootstrap;
}

mojom::CoreServiceCommandPtr StartTask() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "start-task", kGeneration, 0u, kNowMillis + 60'000u, "start-task-key");
  command->kind = mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = mojom::StartTaskCommand::New();
  mojom::StartTaskCommand& start = *command->start_task;
  start.task_id = kTaskId;
  start.browser_profile_id = "profile-1";
  start.browser_session_id = "browser-session-1";
  start.kind = mojom::TaskKind::kResearch;
  start.goal = "exercise durable pause and resume authority";
  start.control_mode = mojom::TaskControlMode::kShared;
  start.provider_route_id = "no_model_required";
  start.assistant_config_version = 1u;
  start.policy_version = 1u;
  start.tool_allowlist = {"browser.dom.read"};
  start.milestone = mojom::TaskMilestone::kM7;
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxSources, 1u));
  // Every start states the per-step retry budget beside its model budget, and
  // the two move together: the core's decoder refuses a command that states
  // one without the other (decision 0218).
  for (mojom::TaskBudgetKind kind :
       {mojom::TaskBudgetKind::kMaxModelRequests,
        mojom::TaskBudgetKind::kMaxInputUnits,
        mojom::TaskBudgetKind::kMaxOutputUnits,
        mojom::TaskBudgetKind::kMaxCostUnits,
        mojom::TaskBudgetKind::kMaxRetriesPerStep}) {
    start.budgets.push_back(mojom::TaskBudget::New(kind, 0u));
  }
  start.trace_id = "start-trace";
  start.task_id_seed.resize(32u);
  for (size_t index = 0u; index < start.task_id_seed.size(); ++index) {
    start.task_id_seed[index] = static_cast<uint8_t>(index + 1u);
  }
  start.template_id = mojom::TaskTemplateId::kBuildSourceTable;
  start.consent_preview = mojom::TaskConsentPreview::New();
  start.consent_preview->sources.push_back(mojom::TaskConsentSource::New(
      "00112233445566778899aabbccddeeff", "tab-a", "https://source.test",
      std::optional<std::string>("https://source.test/page")));
  start.consent_preview->provider_route =
      mojom::TaskProviderRoute::kNoModelRequired;
  start.initial_consent_receipt_id = "initial-consent";
  return command;
}

mojom::CoreServiceCommandPtr PauseTask(uint64_t revision) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "pause-visible-task", kGeneration, revision, kNowMillis + 60'000u,
      "pause-visible-task-key");
  command->kind = mojom::CoreServiceCommandKind::kPauseTask;
  command->pause_task =
      mojom::PauseTaskCommand::New(kTaskId, "pause-visible-task-trace");
  return command;
}

mojom::CoreServiceCommandPtr ResumeTask(uint64_t revision) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "resume-visible-task", kGeneration, revision, kNowMillis + 60'000u,
      "resume-visible-task-key");
  command->kind = mojom::CoreServiceCommandKind::kResumeTask;
  command->resume_task =
      mojom::ResumeTaskCommand::New(kTaskId, "resume-visible-task-trace");
  return command;
}

mojom::EffectResultPtr SuccessfulStorageResult(
    const mojom::EffectEnvelope& effect) {
  if (!effect.operation || !effect.storage_commit) {
    return nullptr;
  }
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kStorageCommit;
  result->storage = mojom::StorageEffectResult::New(
      effect.storage_commit->resulting_revision);
  return result;
}

// Drives the walk to rest, landing every chained commit, and answers the
// revision the last published state binds the task at. A landed commit
// publishes the durable state while the next commit is still chained, so
// the first state is not the walk at rest.
std::optional<uint64_t> StartAndAdvanceToPublishedTask(RustCore* core) {
  CoreResponseBatch batch = core->Submit(StartTask(), kNowMillis);
  std::vector<CoreStatePublication> published;
  for (size_t step = 0u; step < 12u; ++step) {
    if (!batch.admission ||
        batch.admission->status != mojom::AdmissionStatus::kAccepted) {
      ADD_FAILURE() << "task setup was refused at step " << step;
      return std::nullopt;
    }
    if (!batch.states.empty()) {
      published = std::move(batch.states);
    }
    if (batch.effects.empty()) {
      break;
    }
    if (batch.effects.size() != 1u || !batch.effects.front() ||
        batch.effects.front()->kind != mojom::EffectKind::kStorageCommit) {
      ADD_FAILURE() << "task setup yielded an effect other than one commit";
      return std::nullopt;
    }
    auto result = SuccessfulStorageResult(*batch.effects.front());
    if (!result) {
      ADD_FAILURE() << "task setup emitted a malformed storage effect";
      return std::nullopt;
    }
    batch =
        core->DeliverEffectResult(std::move(result), kNowMillis + step + 1u);
  }
  if (published.size() != 1u || !published.front().browser_bindings) {
    ADD_FAILURE() << "task setup did not publish one binding set";
    return std::nullopt;
  }
  const auto& tasks = published.front().browser_bindings->task_revisions;
  const auto task = std::find_if(
      tasks.begin(), tasks.end(),
      [](const auto& row) { return row && row->task_id == kTaskId; });
  if (task == tasks.end()) {
    ADD_FAILURE() << "published bindings omitted the task";
    return std::nullopt;
  }
  return (*task)->task_revision;
}

TEST(RustCoreTaskPauseResumeTest,
     PausingAndPausedWithholdConsentUntilDurableResume) {
  RustCore core;
  CoreInitializationBatch initialized = core.Initialize(ValidBootstrap());
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);
  const std::optional<uint64_t> current = StartAndAdvanceToPublishedTask(&core);
  ASSERT_TRUE(current);

  CoreResponseBatch pause_commit =
      core.Submit(PauseTask(*current), kNowMillis + 20u);
  ASSERT_TRUE(pause_commit.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, pause_commit.admission->status);
  ASSERT_EQ(1u, pause_commit.effects.size());
  auto pause_stored = SuccessfulStorageResult(*pause_commit.effects.front());
  ASSERT_TRUE(pause_stored);
  CoreResponseBatch pausing =
      core.DeliverEffectResult(std::move(pause_stored), kNowMillis + 21u);
  ASSERT_EQ(1u, pausing.states.size());
  ASSERT_TRUE(pausing.states.front().browser_bindings);
  const mojom::CoreStateBrowserBindings& pausing_bindings =
      *pausing.states.front().browser_bindings;
  EXPECT_TRUE(pausing_bindings.accepted_task_consents.empty());
  EXPECT_TRUE(pausing_bindings.committed_action_approvals.empty());
  ASSERT_EQ(1u, pausing_bindings.task_settlements.size());
  ASSERT_TRUE(pausing_bindings.task_settlements.front());
  EXPECT_EQ(mojom::TaskSettlementKind::kPause,
            pausing_bindings.task_settlements.front()->kind);
  CoreStateBindingRegistry registry;
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(pausing_bindings.Clone()));

  CoreResponseBatch settled = core.CompleteTaskSettlement(
      pausing_bindings.task_settlements.front().Clone(), kNowMillis + 22u);
  ASSERT_TRUE(settled.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, settled.admission->status);
  ASSERT_EQ(1u, settled.effects.size());
  auto settlement_stored = SuccessfulStorageResult(*settled.effects.front());
  ASSERT_TRUE(settlement_stored);
  CoreResponseBatch paused =
      core.DeliverEffectResult(std::move(settlement_stored), kNowMillis + 23u);
  ASSERT_EQ(1u, paused.states.size());
  ASSERT_TRUE(paused.states.front().browser_bindings);
  const mojom::CoreStateBrowserBindings& paused_bindings =
      *paused.states.front().browser_bindings;
  EXPECT_TRUE(paused_bindings.accepted_task_consents.empty());
  EXPECT_TRUE(paused_bindings.committed_action_approvals.empty());
  EXPECT_TRUE(paused_bindings.task_settlements.empty());
  ASSERT_EQ(1u, paused_bindings.task_revisions.size());
  ASSERT_TRUE(paused_bindings.task_revisions.front());
  const mojom::TaskRevisionBinding& paused_task =
      *paused_bindings.task_revisions.front();
  EXPECT_NE(std::find(paused_task.allowed_controls.begin(),
                      paused_task.allowed_controls.end(),
                      mojom::TaskControlKind::kResume),
            paused_task.allowed_controls.end());
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(paused_bindings.Clone()));

  CoreResponseBatch resumed =
      core.Submit(ResumeTask(paused_task.task_revision), kNowMillis + 24u);
  ASSERT_TRUE(resumed.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, resumed.admission->status);
  for (uint64_t step = 0u; step < 12u && resumed.states.empty(); ++step) {
    ASSERT_EQ(1u, resumed.effects.size());
    ASSERT_TRUE(resumed.effects.front());
    ASSERT_EQ(mojom::EffectKind::kStorageCommit, resumed.effects.front()->kind);
    auto stored = SuccessfulStorageResult(*resumed.effects.front());
    ASSERT_TRUE(stored);
    resumed =
        core.DeliverEffectResult(std::move(stored), kNowMillis + 25u + step);
    ASSERT_TRUE(resumed.admission);
    ASSERT_EQ(mojom::AdmissionStatus::kAccepted, resumed.admission->status);
  }
  ASSERT_EQ(1u, resumed.states.size());
  ASSERT_TRUE(resumed.states.front().browser_bindings);
  const mojom::CoreStateBrowserBindings& resumed_bindings =
      *resumed.states.front().browser_bindings;
  ASSERT_EQ(1u, resumed_bindings.accepted_task_consents.size());
  EXPECT_TRUE(resumed_bindings.committed_action_approvals.empty());
  EXPECT_TRUE(resumed_bindings.task_settlements.empty());
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(resumed_bindings.Clone()));
}

}  // namespace
}  // namespace taffy
