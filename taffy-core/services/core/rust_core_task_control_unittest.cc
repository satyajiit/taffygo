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
constexpr char kTaskId[] = "task-control-race";

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
  start.goal = "exercise a task control revision race";
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
  auto source = mojom::TaskConsentSource::New();
  source->source_id = "00112233445566778899aabbccddeeff";
  source->tab_id = "tab-a";
  source->normalized_origin = "https://source.test";
  source->canonical_locator = "https://source.test/page";
  start.consent_preview->sources.push_back(std::move(source));
  start.consent_preview->provider_route =
      mojom::TaskProviderRoute::kNoModelRequired;
  start.initial_consent_receipt_id = "initial-consent";
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
  result->storage = mojom::StorageEffectResult::New();
  result->storage->committed_revision =
      effect.storage_commit->resulting_revision;
  return result;
}

// Drives the walk to rest: every chained storage commit is landed, and the
// revision returned is the one the last published state binds the task at.
// A landed commit publishes the durable state while the next commit is still
// chained behind it, so the first state is not the walk at rest and a
// command sent at its revision would meet a commit already in flight.
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
        batch.effects.front()->kind != mojom::EffectKind::kStorageCommit ||
        !batch.effects.front()->storage_commit) {
      ADD_FAILURE() << "task setup yielded an effect other than one commit";
      return std::nullopt;
    }
    mojom::EffectResultPtr result =
        SuccessfulStorageResult(*batch.effects.front());
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
  for (const auto& task : published.front().browser_bindings->task_revisions) {
    if (!task || task->task_id != kTaskId) {
      continue;
    }
    if (std::find(task->allowed_controls.begin(), task->allowed_controls.end(),
                  mojom::TaskControlKind::kStop) ==
        task->allowed_controls.end()) {
      ADD_FAILURE() << "published task did not expose Stop";
      return std::nullopt;
    }
    return task->task_revision;
  }
  ADD_FAILURE() << "published bindings omitted the task";
  return std::nullopt;
}

mojom::CoreServiceCommandPtr CancelTask(uint64_t revision,
                                        std::string operation_id,
                                        std::string idempotency_key) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      std::move(operation_id), kGeneration, revision, kNowMillis + 60'000u,
      std::move(idempotency_key));
  command->kind = mojom::CoreServiceCommandKind::kCancelTask;
  command->cancel_task = mojom::CancelTaskCommand::New(
      kTaskId, mojom::CancelReason::kUser, "cancel-trace");
  return command;
}

TEST(RustCoreTaskControlTest,
     OlderVisibleCancelBindsAtomicallyToTheCurrentRevision) {
  RustCore core;
  CoreInitializationBatch initialized = core.Initialize(ValidBootstrap());
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);
  const std::optional<uint64_t> current = StartAndAdvanceToPublishedTask(&core);
  ASSERT_TRUE(current);
  ASSERT_GT(*current, 1u);

  constexpr char kOperationId[] = "cancel-visible-task";
  constexpr char kIdempotencyKey[] = "cancel-visible-task-key";
  mojom::CoreServiceCommandPtr command =
      CancelTask(*current - 1u, kOperationId, kIdempotencyKey);
  CoreResponseBatch cancelled = core.Submit(command.Clone(), kNowMillis + 20u);

  ASSERT_TRUE(cancelled.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, cancelled.admission->status);
  EXPECT_EQ(*current - 1u, command->operation->task_revision);
  ASSERT_EQ(1u, cancelled.effects.size());
  const mojom::EffectEnvelope& commit = *cancelled.effects.front();
  ASSERT_TRUE(commit.operation);
  ASSERT_TRUE(commit.storage_commit);
  EXPECT_EQ(mojom::EffectKind::kStorageCommit, commit.kind);
  EXPECT_EQ(mojom::StorageOperation::kAppendTaskCommit,
            commit.storage_commit->operation_kind);
  EXPECT_EQ(*current, commit.storage_commit->expected_revision);
  EXPECT_GT(commit.storage_commit->resulting_revision, *current);
  EXPECT_EQ(commit.storage_commit->resulting_revision,
            commit.operation->task_revision);
  EXPECT_EQ(kOperationId, commit.effect_id);
  EXPECT_EQ(kOperationId, commit.operation->operation_id);
  EXPECT_EQ(kIdempotencyKey, commit.operation->idempotency_key);

  mojom::EffectResultPtr stored = SuccessfulStorageResult(commit);
  ASSERT_TRUE(stored);
  CoreResponseBatch durable =
      core.DeliverEffectResult(std::move(stored), kNowMillis + 21u);
  ASSERT_TRUE(durable.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, durable.admission->status);

  CoreResponseBatch duplicate = core.Submit(command.Clone(), kNowMillis + 22u);
  ASSERT_TRUE(duplicate.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kDuplicate, duplicate.admission->status);
  EXPECT_TRUE(duplicate.effects.empty());
  EXPECT_TRUE(duplicate.states.empty());
}

TEST(RustCoreTaskControlTest,
     FutureMalformedAndNonCancelCommandsKeepExactRevisionAdmission) {
  RustCore core;
  CoreInitializationBatch initialized = core.Initialize(ValidBootstrap());
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);
  const std::optional<uint64_t> current = StartAndAdvanceToPublishedTask(&core);
  ASSERT_TRUE(current);
  ASSERT_GT(*current, 1u);

  CoreResponseBatch future = core.Submit(
      CancelTask(*current + 1u, "future-cancel", "future-cancel-key"),
      kNowMillis + 20u);
  ASSERT_TRUE(future.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kStaleRevision, future.admission->status);
  EXPECT_TRUE(future.effects.empty());
  EXPECT_TRUE(future.states.empty());

  mojom::CoreServiceCommandPtr malformed =
      CancelTask(*current - 1u, "malformed-cancel", "malformed-cancel-key");
  malformed->cancel_task->reason = static_cast<mojom::CancelReason>(99u);
  CoreResponseBatch malformed_result =
      core.Submit(std::move(malformed), kNowMillis + 21u);
  ASSERT_TRUE(malformed_result.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kStaleRevision,
            malformed_result.admission->status);
  EXPECT_TRUE(malformed_result.effects.empty());

  mojom::CoreServiceCommandPtr malformed_current = CancelTask(
      *current, "malformed-current-cancel", "malformed-current-cancel-key");
  malformed_current->cancel_task->reason =
      static_cast<mojom::CancelReason>(99u);
  CoreResponseBatch malformed_current_result =
      core.Submit(std::move(malformed_current), kNowMillis + 22u);
  ASSERT_TRUE(malformed_current_result.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kInvalidCommand,
            malformed_current_result.admission->status);
  EXPECT_TRUE(malformed_current_result.effects.empty());

  auto pause = mojom::CoreServiceCommand::New();
  pause->operation =
      mojom::OperationEnvelope::New("stale-pause", kGeneration, *current - 1u,
                                    kNowMillis + 60'000u, "stale-pause-key");
  pause->kind = mojom::CoreServiceCommandKind::kPauseTask;
  pause->pause_task =
      mojom::PauseTaskCommand::New(kTaskId, "stale-pause-trace");
  CoreResponseBatch stale_pause =
      core.Submit(std::move(pause), kNowMillis + 23u);
  ASSERT_TRUE(stale_pause.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kStaleRevision,
            stale_pause.admission->status);
  EXPECT_TRUE(stale_pause.effects.empty());
}

TEST(RustCoreTaskControlTest,
     OneStopWaitsBehindTheCurrentCommitAndPreemptsItsEffects) {
  RustCore core;
  CoreInitializationBatch initialized = core.Initialize(ValidBootstrap());
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);

  CoreResponseBatch opening = core.Submit(StartTask(), kNowMillis);
  ASSERT_TRUE(opening.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, opening.admission->status);
  ASSERT_EQ(1u, opening.effects.size());
  mojom::EffectResultPtr opened =
      SuccessfulStorageResult(*opening.effects.front());
  ASSERT_TRUE(opened);

  CoreResponseBatch held =
      core.DeliverEffectResult(std::move(opened), kNowMillis + 1u);
  ASSERT_TRUE(held.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, held.admission->status);
  // The landed open commit is published as the durable state, beside the
  // commit the walk chains behind it.
  ASSERT_EQ(1u, held.states.size());
  ASSERT_EQ(1u, held.effects.size());
  ASSERT_TRUE(held.effects.front());
  ASSERT_TRUE(held.effects.front()->storage_commit);
  const mojom::EffectEnvelopePtr held_commit = held.effects.front().Clone();
  const uint64_t visible_revision =
      held_commit->storage_commit->expected_revision;
  ASSERT_GT(visible_revision, 0u);

  mojom::CoreServiceCommandPtr stop =
      CancelTask(visible_revision, "queued-stop", "queued-stop-key");
  CoreResponseBatch queued = core.Submit(stop.Clone(), kNowMillis + 2u);
  ASSERT_TRUE(queued.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, queued.admission->status);
  EXPECT_TRUE(queued.effects.empty());
  EXPECT_TRUE(queued.states.empty());

  CoreResponseBatch duplicate = core.Submit(stop.Clone(), kNowMillis + 3u);
  ASSERT_TRUE(duplicate.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kDuplicate, duplicate.admission->status);
  EXPECT_TRUE(duplicate.effects.empty());

  mojom::EffectResultPtr committed = SuccessfulStorageResult(*held_commit);
  ASSERT_TRUE(committed);
  CoreResponseBatch cancellation =
      core.DeliverEffectResult(std::move(committed), kNowMillis + 4u);
  ASSERT_TRUE(cancellation.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, cancellation.admission->status);
  // The queued stop preempts the walk: its own commit is chained at once and
  // the state is published when that commit lands, not here.
  EXPECT_TRUE(cancellation.states.empty());
  ASSERT_EQ(1u, cancellation.effects.size());
  ASSERT_TRUE(cancellation.effects.front());
  ASSERT_TRUE(cancellation.effects.front()->storage_commit);
  EXPECT_EQ("queued-stop", cancellation.effects.front()->effect_id);
  EXPECT_EQ(mojom::StorageOperation::kAppendTaskCommit,
            cancellation.effects.front()->storage_commit->operation_kind);
  EXPECT_EQ(held_commit->storage_commit->resulting_revision,
            cancellation.effects.front()->storage_commit->expected_revision);

  mojom::EffectResultPtr cancellation_stored =
      SuccessfulStorageResult(*cancellation.effects.front());
  ASSERT_TRUE(cancellation_stored);
  CoreResponseBatch settling =
      core.DeliverEffectResult(std::move(cancellation_stored), kNowMillis + 5u);
  ASSERT_TRUE(settling.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, settling.admission->status);
  ASSERT_EQ(1u, settling.states.size());
  ASSERT_TRUE(settling.states.front().browser_bindings);
  const mojom::CoreStateBrowserBindings& settling_bindings =
      *settling.states.front().browser_bindings;
  EXPECT_TRUE(settling_bindings.accepted_task_consents.empty());
  EXPECT_TRUE(settling_bindings.committed_action_approvals.empty());
  ASSERT_EQ(1u, settling_bindings.task_settlements.size());
  ASSERT_TRUE(settling_bindings.task_settlements.front());
  EXPECT_EQ(mojom::TaskSettlementKind::kCancel,
            settling_bindings.task_settlements.front()->kind);

  CoreStateBindingRegistry registry;
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(settling_bindings.Clone()));

  CoreResponseBatch settlement_commit = core.CompleteTaskSettlement(
      settling_bindings.task_settlements.front().Clone(), kNowMillis + 6u);
  ASSERT_TRUE(settlement_commit.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted,
            settlement_commit.admission->status);
  ASSERT_EQ(1u, settlement_commit.effects.size());
  ASSERT_TRUE(settlement_commit.effects.front());
  ASSERT_TRUE(settlement_commit.effects.front()->storage_commit);

  mojom::EffectResultPtr settlement_stored =
      SuccessfulStorageResult(*settlement_commit.effects.front());
  ASSERT_TRUE(settlement_stored);
  CoreResponseBatch terminal =
      core.DeliverEffectResult(std::move(settlement_stored), kNowMillis + 7u);
  ASSERT_TRUE(terminal.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, terminal.admission->status);
  ASSERT_EQ(1u, terminal.states.size());
  ASSERT_TRUE(terminal.states.front().browser_bindings);
  const mojom::CoreStateBrowserBindings& terminal_bindings =
      *terminal.states.front().browser_bindings;
  EXPECT_TRUE(terminal_bindings.accepted_task_consents.empty());
  EXPECT_TRUE(terminal_bindings.committed_action_approvals.empty());
  ASSERT_EQ(1u, terminal_bindings.terminal_tasks.size());
  ASSERT_TRUE(terminal_bindings.terminal_tasks.front());
  EXPECT_EQ(mojom::TerminalTaskKind::kCancelled,
            terminal_bindings.terminal_tasks.front()->kind);
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(terminal_bindings.Clone()));
}

}  // namespace
}  // namespace taffy
