// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/rust_workspace_snapshot_compat_test_support.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "taffy/browser/core_task_policy.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/core/rust_core.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {

namespace mojom = core_service::mojom;

mojom::CoreBootstrapPtr MakeWorkspaceSnapshotBootstrap() {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = kWorkspaceSnapshotGeneration;
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

mojom::CoreServiceCommandPtr MakeWorkspaceSnapshotStartTask() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "open-task", kWorkspaceSnapshotGeneration, 0u,
      kWorkspaceSnapshotNowMillis + 60'000u, "create-task");
  command->kind = mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = mojom::StartTaskCommand::New();
  mojom::StartTaskCommand& start = *command->start_task;
  start.task_id = "task-a";
  start.browser_profile_id = "profile-1";
  start.browser_session_id = "browser-session-1";
  start.kind = mojom::TaskKind::kResearch;
  start.goal = "build a source table";
  start.control_mode = mojom::TaskControlMode::kShared;
  start.provider_route_id = "no_model_required";
  start.assistant_config_version = 1u;
  start.policy_version = 1u;
  start.tool_allowlist = {"browser.dom.read"};
  start.milestone = mojom::TaskMilestone::kM7;
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxSources, 1u));
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxModelRequests, 0u));
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxInputUnits, 0u));
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxOutputUnits, 0u));
  // Every start states the per-step retry budget beside its model budget, and
  // the two move together: the core's decoder refuses a command that states
  // one without the other (decision 0218).
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxCostUnits, 0u));
  start.budgets.push_back(
      mojom::TaskBudget::New(mojom::TaskBudgetKind::kMaxRetriesPerStep, 0u));
  start.has_task_deadline = false;
  start.trace_id = "trace-a";
  start.task_id_seed.resize(32u);
  for (size_t index = 0u; index < start.task_id_seed.size(); ++index) {
    start.task_id_seed[index] = static_cast<uint8_t>(index + 1u);
  }
  start.template_id = mojom::TaskTemplateId::kBuildSourceTable;
  start.consent_preview = mojom::TaskConsentPreview::New();
  auto source = mojom::TaskConsentSource::New();
  source->source_id = "00112233445566778899aabbccddeeff";
  source->tab_id = "tab-a";
  source->normalized_origin = "http://source.test:12345";
  source->canonical_locator = "http://source.test:12345/title1.html";
  start.consent_preview->sources.push_back(std::move(source));
  start.consent_preview->provider_route =
      mojom::TaskProviderRoute::kNoModelRequired;
  start.initial_consent_receipt_id = "initial-consent-a";
  return command;
}

mojom::CoreServiceCommandPtr MakeWorkspaceSnapshotApproval(
    const PendingApprovalLookup& pending,
    const std::string& action_id) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "approve-action", kWorkspaceSnapshotGeneration, pending.task_revision,
      kWorkspaceSnapshotNowMillis + 60'000u, "approve-action-key");
  command->kind = mojom::CoreServiceCommandKind::kUserDecision;
  command->user_decision = mojom::UserDecisionCommand::New(
      "task-a", action_id, mojom::UserDecisionKind::kAccept,
      pending.proposal_digest, "approve-trace", "approval-receipt-a",
      command->operation->deadline_monotonic_ms,
      kWorkspaceSnapshotApprovalExpiresAtUtcMillis, "browser-session-1");
  return command;
}

mojom::EffectResultPtr DispatchWorkspaceSnapshotEffect(
    CoreEffectBroker* broker,
    mojom::EffectEnvelopePtr effect) {
  base::RunLoop loop;
  mojom::EffectResultPtr result;
  broker->Dispatch(std::move(effect), base::BindLambdaForTesting(
                                          [&](mojom::EffectResultPtr terminal) {
                                            result = std::move(terminal);
                                            loop.Quit();
                                          }));
  loop.Run();
  return result;
}

mojom::CoreBootstrapPtr LoadWorkspaceSnapshotBootstrap(
    CoreStorageBroker* broker) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      kWorkspaceSnapshotGeneration, false,
      base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr value) {
        bootstrap = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

mojom::PolicyEvaluationResultPtr EvaluateWorkspaceSnapshotReadPolicy(
    RustCore* core,
    const mojom::TaskPolicyEffect& policy,
    uint64_t now_monotonic_ms,
    uint64_t graph_revision) {
  const ActorLeaseResult lease{
      .code = ActorLeaseResultCode::kIssued,
      .lease_id = ActorLeaseId{"lease-a"},
      .expires_at_monotonic_ms = kWorkspaceSnapshotNowMillis + 30'000u,
  };
  const TaskPolicyDocumentBinding document{
      .tab_id = policy.tab_id,
      .frame_id = "frame-a",
      .page_epoch = "epoch-a",
      .origin = "http://source.test:12345",
      .graph_revision = graph_revision,
  };
  mojom::PolicyEvaluationRequestPtr request = BindReadOnlyTaskPolicyRequest(
      policy, document, "profile-1", lease, kWorkspaceSnapshotGeneration,
      now_monotonic_ms, kWorkspaceSnapshotNowUtcMillis);
  if (!request) {
    return nullptr;
  }
  return core->EvaluatePolicy(std::move(request));
}

void VerifyWorkspaceSnapshotTerminalSaveAndRestore(
    RustCore* core,
    CoreEffectBroker* broker,
    CoreStorageBroker* storage,
    mojom::TaskEffectBindingPtr approved_policy_effect,
    uint64_t now_monotonic_ms) {
  ASSERT_TRUE(core);
  ASSERT_TRUE(broker);
  ASSERT_TRUE(storage);
  ASSERT_TRUE(approved_policy_effect);
  ASSERT_TRUE(approved_policy_effect->operation);
  const uint64_t approved_revision =
      approved_policy_effect->operation->task_revision;
  const WorkspaceSnapshotObservationResult observation =
      ExerciseWorkspaceSnapshotObservation(core, broker,
                                           approved_policy_effect.Clone(),
                                           now_monotonic_ms);
  // Asserted field by field rather than through one predicate: this sequence
  // has six independent ways to go wrong and a single boolean names none of
  // them, which is the whole cost of diagnosing it from a log.
  EXPECT_EQ(mojom::PolicyEvaluationStatus::kGranted, observation.policy_status);
  EXPECT_EQ(mojom::TaskReducerEffectKind::kDispatchAction,
            observation.action_kind);
  EXPECT_EQ(0u, observation.grant_graph_revision);
  EXPECT_EQ(mojom::AdmissionStatus::kInvalidCommand,
            observation.below_floor_status);
  EXPECT_EQ(1u, observation.observation_graph_revision);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, observation.observation_status);
  EXPECT_EQ(6u, observation.post_observation_commit_count);
  EXPECT_EQ(mojom::TaskReducerEffectKind::kReleaseTaskTabs,
            observation.terminal_effect_kind);
  EXPECT_EQ(approved_revision + 8u, observation.terminal_task_revision);

  // Reload only after the task is terminal. This is the bootstrap a
  // replacement core generation actually receives.
  mojom::CoreBootstrapPtr terminal_bootstrap =
      LoadWorkspaceSnapshotBootstrap(storage);
  ASSERT_TRUE(terminal_bootstrap);
  const auto terminal_task = std::find_if(
      terminal_bootstrap->tasks.begin(), terminal_bootstrap->tasks.end(),
      [](const mojom::TaskRestoreRecordPtr& task) {
        return task && task->task_id == "task-a";
      });
  ASSERT_NE(terminal_bootstrap->tasks.end(), terminal_task);
  ASSERT_FALSE((*terminal_task)->batches.empty());
  EXPECT_EQ(observation.terminal_task_revision,
            (*terminal_task)->batches.back()->resulting_revision);
  const auto terminal_workspace = std::find_if(
      terminal_bootstrap->workspaces.begin(),
      terminal_bootstrap->workspaces.end(),
      [](const mojom::WorkspaceRestoreRecordPtr& value) {
        return value && !value->workspace_id.empty();
      });
  ASSERT_NE(terminal_bootstrap->workspaces.end(), terminal_workspace);
  const std::string terminal_workspace_id = (*terminal_workspace)->workspace_id;
  const uint64_t terminal_workspace_revision = (*terminal_workspace)->revision;

  auto save = mojom::CoreServiceCommand::New();
  save->operation = mojom::OperationEnvelope::New(
      "save-terminal-workspace", kWorkspaceSnapshotGeneration, 0u,
      kWorkspaceSnapshotNowMillis + 120'000u, "save-terminal-workspace-key");
  save->kind = mojom::CoreServiceCommandKind::kSaveWorkspace;
  save->save_workspace = mojom::SaveWorkspaceCommand::New(
      terminal_workspace_id, terminal_workspace_revision);
  CoreResponseBatch save_submitted =
      core->Submit(std::move(save), kWorkspaceSnapshotNowMillis + 100u);
  ASSERT_TRUE(save_submitted.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted,
            save_submitted.admission->status);
  ASSERT_EQ(1u, save_submitted.effects.size());
  ASSERT_TRUE(save_submitted.effects.front()->storage_commit);
  EXPECT_EQ(mojom::StorageOperation::kUpsertWorkspace,
            save_submitted.effects.front()->storage_commit->operation_kind);
  EXPECT_EQ(std::vector<uint8_t>(32u, 0u),
            save_submitted.effects.front()->storage_commit->task_id_seed);
  mojom::EffectResultPtr save_result = DispatchWorkspaceSnapshotEffect(
      broker, save_submitted.effects.front().Clone());
  ASSERT_TRUE(save_result);
  ASSERT_EQ(mojom::EffectStatus::kCompleted, save_result->status);
  CoreResponseBatch save_completed = core->DeliverEffectResult(
      std::move(save_result), kWorkspaceSnapshotNowMillis + 101u);
  ASSERT_TRUE(save_completed.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted,
            save_completed.admission->status);
  ASSERT_EQ(1u, save_completed.states.size());

  mojom::CoreBootstrapPtr saved_bootstrap =
      LoadWorkspaceSnapshotBootstrap(storage);
  ASSERT_TRUE(saved_bootstrap);
  const auto saved_task = std::find_if(
      saved_bootstrap->tasks.begin(), saved_bootstrap->tasks.end(),
      [](const mojom::TaskRestoreRecordPtr& task) {
        return task && task->task_id == "task-a";
      });
  ASSERT_NE(saved_bootstrap->tasks.end(), saved_task);
  const auto saved_workspace = std::find_if(
      saved_bootstrap->workspaces.begin(), saved_bootstrap->workspaces.end(),
      [&terminal_workspace_id](const mojom::WorkspaceRestoreRecordPtr& value) {
        return value && value->workspace_id == terminal_workspace_id;
      });
  ASSERT_NE(saved_bootstrap->workspaces.end(), saved_workspace);
  EXPECT_GT((*saved_workspace)->revision, terminal_workspace_revision);

  // The broker creates its profile identity lazily in this isolated seam
  // fixture. Production loads it before the Rust core is initialized.
  saved_bootstrap->browser_profile_id = "profile-1";
  saved_bootstrap->browser_session_id = "browser-session-1";
  saved_bootstrap->service_generation = kWorkspaceSnapshotGeneration + 1u;
  saved_bootstrap->generation_capability_entropy.resize(32u);
  for (size_t index = 0u;
       index < saved_bootstrap->generation_capability_entropy.size(); ++index) {
    saved_bootstrap->generation_capability_entropy[index] =
        static_cast<uint8_t>(32u - index);
  }

  RustCore replacement;
  CoreInitializationBatch replacement_initialized =
      replacement.Initialize(std::move(saved_bootstrap));
  ASSERT_TRUE(replacement_initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady,
            replacement_initialized.result->status);
  EXPECT_EQ(kWorkspaceSnapshotGeneration + 1u,
            replacement_initialized.result->accepted_generation);
  ASSERT_EQ(1u, replacement_initialized.states.size());
  const CoreStatePublication& replacement_state =
      replacement_initialized.states.front();
  ASSERT_TRUE(replacement_state.state);
  EXPECT_EQ(kWorkspaceSnapshotGeneration + 1u,
            replacement_state.state->service_generation);
  EXPECT_EQ(1u, replacement_state.state->sequence);
  EXPECT_FALSE(replacement_state.state->payload.empty());
  ASSERT_TRUE(replacement_state.browser_bindings);
  const auto replacement_task = std::find_if(
      replacement_state.browser_bindings->terminal_tasks.begin(),
      replacement_state.browser_bindings->terminal_tasks.end(),
      [](const mojom::TerminalTaskBindingPtr& task) {
        return task && task->task_id == "task-a";
      });
  ASSERT_NE(replacement_state.browser_bindings->terminal_tasks.end(),
            replacement_task);
  EXPECT_EQ(observation.terminal_task_revision,
            (*replacement_task)->task_revision);
  const auto release_tabs = std::find_if(
      replacement_state.task_effects.begin(),
      replacement_state.task_effects.end(),
      [](const mojom::TaskEffectBindingPtr& effect) {
        return effect && effect->task_id == "task-a" &&
               effect->kind == mojom::TaskReducerEffectKind::kReleaseTaskTabs;
      });
  ASSERT_NE(replacement_state.task_effects.end(), release_tabs);
}

}  // namespace taffy::test
