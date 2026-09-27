// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/browser/core_task_policy.h"
#include "taffy/browser/policy_response_validation.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"
#include "taffy/test/support/rust_workspace_snapshot_compat_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = test::kWorkspaceSnapshotGeneration;
constexpr uint64_t kNowMillis = test::kWorkspaceSnapshotNowMillis;
constexpr uint64_t kNowUtcMillis = test::kWorkspaceSnapshotNowUtcMillis;

TEST(RustWorkspaceSnapshotCompatibilityTest,
     CreationCommitIsAcceptedAndRestoredByNativeStorage) {
  base::test::TaskEnvironment task_environment;
  RustCore core;
  CoreInitializationBatch initialized =
      core.Initialize(test::MakeWorkspaceSnapshotBootstrap());
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);

  CoreStateBindingRegistry bindings;
  auto empty_bindings = mojom::CoreStateBrowserBindings::New();
  empty_bindings->service_generation = kGeneration;
  empty_bindings->state_sequence = 1u;
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            bindings.Replace(std::move(empty_bindings)));
  AcceptedApprovalLedger ledger;
  mojom::CoreServiceCommandPtr command = test::MakeWorkspaceSnapshotStartTask();
  ASSERT_EQ(
      AuthoritySubmissionStage::kStaged,
      ledger.StageSubmittedCommand(*command, "profile-1", "browser-session-1",
                                   bindings, kNowMillis, 1'800'000'000'000u));

  CoreResponseBatch submitted = core.Submit(command.Clone(), kNowMillis);
  ASSERT_TRUE(submitted.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, submitted.admission->status);
  ASSERT_EQ(1u, submitted.effects.size());
  const mojom::EffectEnvelope& effect = *submitted.effects.front();
  ASSERT_EQ(mojom::EffectKind::kStorageCommit, effect.kind);
  ASSERT_TRUE(effect.storage_commit);
  ASSERT_EQ(mojom::StorageOperation::kAppendTaskCommit,
            effect.storage_commit->operation_kind);
  ASSERT_TRUE(effect.storage_commit->workspace);
  EXPECT_EQ(0u, effect.storage_commit->workspace->expected_revision);
  EXPECT_EQ(1u, effect.storage_commit->workspace->resulting_revision);
  ASSERT_EQ(AuthorityStorageBinding::kBound,
            ledger.BindStorageCommit(effect, kNowMillis));
  std::string expected_workspace_id =
      effect.storage_commit->workspace->workspace_id;
  uint64_t expected_workspace_revision =
      effect.storage_commit->workspace->resulting_revision;
  std::vector<uint8_t> expected_workspace_snapshot =
      effect.storage_commit->workspace->snapshot;

  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker storage(directory.GetPath().AppendASCII("core.sqlite3"),
                            false);
  CoreEffectBroker::Handlers handlers;
  handlers.storage = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr storage_effect,
          CoreEffectBroker::CompletionCallback done) {
        storage.DispatchStorage(std::move(storage_effect), std::move(done));
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(kGeneration);
  mojom::EffectResultPtr result =
      test::DispatchWorkspaceSnapshotEffect(&broker, effect.Clone());
  ASSERT_TRUE(result);
  ASSERT_EQ(mojom::EffectStatus::kCompleted, result->status);
  EXPECT_EQ(effect.storage_commit->resulting_revision,
            result->storage->committed_revision);
  ASSERT_EQ(AuthorityStorageCompletion::kCommitted,
            ledger.RecordStorageCompletion(*result));

  CoreResponseBatch completed =
      core.DeliverEffectResult(result.Clone(), kNowMillis + 1u);
  ASSERT_TRUE(completed.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, completed.admission->status);
  // Land every chained commit until the walk rests: a landed commit publishes
  // the durable state while the next commit is still chained behind it, so
  // the walk has stopped only when the effect list carries no commit.
  size_t storage_commit_count = 1u;
  std::vector<CoreStatePublication> published = std::move(completed.states);
  for (size_t index = 0u; !completed.effects.empty() &&
                          completed.effects.front() &&
                          completed.effects.front()->kind ==
                              mojom::EffectKind::kStorageCommit &&
                          index < 8u;
       ++index) {
    ASSERT_EQ(1u, completed.effects.size());
    mojom::EffectEnvelopePtr continued = completed.effects.front().Clone();
    ASSERT_TRUE(continued->storage_commit);
    ASSERT_EQ(mojom::StorageOperation::kAppendTaskCommit,
              continued->storage_commit->operation_kind);
    EXPECT_EQ(AuthorityStorageBinding::kNotTracked,
              ledger.BindStorageCommit(*continued, kNowMillis + index + 1u));
    if (continued->storage_commit->workspace) {
      expected_workspace_id =
          continued->storage_commit->workspace->workspace_id;
      expected_workspace_revision =
          continued->storage_commit->workspace->resulting_revision;
      expected_workspace_snapshot =
          continued->storage_commit->workspace->snapshot;
    }
    mojom::EffectResultPtr continued_result =
        test::DispatchWorkspaceSnapshotEffect(&broker, continued.Clone());
    ASSERT_TRUE(continued_result);
    ASSERT_EQ(mojom::EffectStatus::kCompleted, continued_result->status);
    EXPECT_EQ(AuthorityStorageCompletion::kNotTracked,
              ledger.RecordStorageCompletion(*continued_result));
    completed = core.DeliverEffectResult(std::move(continued_result),
                                         kNowMillis + index + 2u);
    ASSERT_TRUE(completed.admission);
    ASSERT_EQ(mojom::AdmissionStatus::kAccepted, completed.admission->status);
    ++storage_commit_count;
    if (!completed.states.empty()) {
      published = std::move(completed.states);
    }
  }
  ASSERT_FALSE(published.empty());
  EXPECT_GE(storage_commit_count, 2u);

  // This is the first executable boundary of the built-in source-table
  // workflow. Keep the real Rust projection coupled to the browser's shape
  // validator: independently valid fixtures on either side once allowed a
  // malformed policy ask to end the isolated generation only on a phone.
  ASSERT_EQ(1u, published.size());
  CoreStatePublication& publication = published.front();
  ASSERT_TRUE(publication.browser_bindings);
  ASSERT_EQ(1u, publication.task_effects.size());
  const mojom::TaskEffectBinding& task_effect =
      *publication.task_effects.front();
  ASSERT_EQ(mojom::TaskReducerEffectKind::kAskPolicy, task_effect.kind);
  ASSERT_TRUE(task_effect.policy);
  const mojom::TaskPolicyEffect& policy = *task_effect.policy;
  ASSERT_TRUE(policy.operation);
  EXPECT_EQ(task_effect.operation, policy.operation);
  EXPECT_EQ(task_effect.effect_id, policy.effect_id);

  CoreStateBindingRegistry final_bindings;
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            final_bindings.Replace(publication.browser_bindings.Clone()));
  ledger.Reconcile(final_bindings, kGeneration, kNowMillis + 16u);
  ledger.RehydrateDurableAuthority(
      *publication.browser_bindings, final_bindings, kGeneration,
      "browser-session-1", kNowMillis + 16u, 1'800'000'000'000u,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));
  const std::optional<uint64_t> final_revision =
      final_bindings.FindTaskRevision("task-a");
  ASSERT_TRUE(final_revision);
  SCOPED_TRACE(
      testing::Message()
      << "task_revision=" << *final_revision
      << ", operation_revision=" << policy.operation->task_revision
      << ", operation_kind=" << static_cast<uint32_t>(policy.operation_kind)
      << ", action_class=" << static_cast<uint32_t>(policy.action_class)
      << ", context_risk=" << static_cast<uint32_t>(policy.context_risk)
      << ", control_mode=" << static_cast<uint32_t>(policy.control_mode)
      << ", tool=" << policy.tool_name << ", tab=" << policy.tab_id
      << ", node=" << policy.node_id.value_or("<none>")
      << ", destination=" << policy.destination_address.value_or("<none>")
      << ", search=" << policy.transient_search_query.value_or("<none>")
      << ", approval=" << static_cast<bool>(policy.approval)
      << ", data_classes=" << policy.data_classes.size());
  EXPECT_EQ(kGeneration, policy.operation->service_generation);
  EXPECT_EQ(*final_revision, policy.operation->task_revision);
  EXPECT_GT(policy.operation->deadline_monotonic_ms, kNowMillis + 16u);
  EXPECT_NE(policy.effect_id, policy.operation->operation_id);
  EXPECT_EQ(policy.idempotency_key, policy.operation->idempotency_key);
  EXPECT_TRUE(TaskOperationMatchesClassAndTool(
      policy.operation_kind, policy.action_class, policy.tool_name));
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      policy.input.get(), policy.operation_kind, policy.canonical_intent,
      policy.tab_id, policy.node_id));
  EXPECT_EQ(64u, policy.proposal_digest.size());
  EXPECT_TRUE(policy.principal);
  EXPECT_FALSE(policy.data_classes.empty());
  EXPECT_NE(0u, policy.policy_version);
  EXPECT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      policy, kGeneration, *final_revision, "browser-session-1",
      kNowMillis + 16u, 1'800'000'000'000u));
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(policy, "http://source.test:12345",
                                            kGeneration));

  mojom::PolicyEvaluationResultPtr policy_result =
      test::EvaluateWorkspaceSnapshotReadPolicy(&core, policy,
                                                kNowMillis + 16u);
  ASSERT_TRUE(policy_result);
  ASSERT_EQ(mojom::PolicyEvaluationStatus::kApprovalRequired,
            policy_result->status);
  EXPECT_FALSE(policy_result->minted_grant);

  CoreResponseBatch policy_completed =
      core.CompleteTaskPolicy(publication.task_effects.front().Clone(),
                              policy_result.Clone(), kNowMillis + 17u);
  ASSERT_TRUE(policy_completed.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted,
            policy_completed.admission->status);
  EXPECT_TRUE(policy_completed.states.empty());
  ASSERT_EQ(1u, policy_completed.effects.size());
  EXPECT_EQ(mojom::EffectKind::kStorageCommit,
            policy_completed.effects.front()->kind);

  // The policy decision is not visible until its journal append is durable.
  // Drive that exact append through the native writer, then validate the first
  // state which can carry the pending approval. The device vertical used to
  // end its isolated generation between these two points, while this test
  // stopped at the storage effect above and therefore could not reproduce the
  // failure.
  mojom::EffectEnvelopePtr policy_commit =
      policy_completed.effects.front().Clone();
  ASSERT_TRUE(policy_commit);
  ASSERT_TRUE(policy_commit->storage_commit);
  EXPECT_EQ(mojom::StorageOperation::kAppendTaskCommit,
            policy_commit->storage_commit->operation_kind);
  if (policy_commit->storage_commit->workspace) {
    expected_workspace_id =
        policy_commit->storage_commit->workspace->workspace_id;
    expected_workspace_revision =
        policy_commit->storage_commit->workspace->resulting_revision;
    expected_workspace_snapshot =
        policy_commit->storage_commit->workspace->snapshot;
  }
  EXPECT_EQ(AuthorityStorageBinding::kNotTracked,
            ledger.BindStorageCommit(*policy_commit, kNowMillis + 18u));
  mojom::EffectResultPtr policy_commit_result =
      test::DispatchWorkspaceSnapshotEffect(&broker, policy_commit.Clone());
  ++storage_commit_count;
  ASSERT_TRUE(policy_commit_result);
  ASSERT_EQ(mojom::EffectStatus::kCompleted, policy_commit_result->status);
  EXPECT_EQ(AuthorityStorageCompletion::kNotTracked,
            ledger.RecordStorageCompletion(*policy_commit_result));

  CoreResponseBatch approval_published = core.DeliverEffectResult(
      std::move(policy_commit_result), kNowMillis + 19u);
  ASSERT_TRUE(approval_published.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted,
            approval_published.admission->status);
  ASSERT_TRUE(approval_published.effects.empty());
  ASSERT_EQ(1u, approval_published.states.size());
  CoreStatePublication& approval_publication =
      approval_published.states.front();
  ASSERT_TRUE(approval_publication.browser_bindings);
  ASSERT_EQ(1u, approval_publication.task_effects.size());
  EXPECT_EQ(mojom::TaskReducerEffectKind::kRequestApproval,
            approval_publication.task_effects.front()->kind);
  ASSERT_EQ(1u,
            approval_publication.browser_bindings->pending_approvals.size());
  const mojom::PendingApprovalBinding& pending_approval =
      *approval_publication.browser_bindings->pending_approvals.front();
  EXPECT_EQ(task_effect.task_id, pending_approval.task_id);
  EXPECT_EQ(task_effect.policy->action_id, pending_approval.action_id);
  EXPECT_EQ(task_effect.policy->proposal_digest,
            pending_approval.proposal_digest);

  CoreStateBindingRegistry approval_bindings;
  ASSERT_EQ(
      mojom::PendingApprovalRegistrationStatus::kRegistered,
      approval_bindings.Replace(approval_publication.browser_bindings.Clone()));
  const std::optional<uint64_t> approval_revision =
      approval_bindings.FindTaskRevision(task_effect.task_id);
  ASSERT_TRUE(approval_revision);
  // CoreServiceImpl retires AskPolicy only when the state causally tagged by
  // this exact storage terminal carries the commit's declared result.
  EXPECT_EQ(policy_commit->storage_commit->resulting_revision,
            *approval_revision);
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *approval_publication.task_effects.front(), kGeneration,
      *approval_revision, kNowMillis + 19u));
  const std::optional<PendingApprovalLookup> pending_lookup =
      approval_bindings.FindPendingApproval(task_effect.task_id,
                                            task_effect.policy->action_id);
  ASSERT_TRUE(pending_lookup);
  EXPECT_EQ(*approval_revision, pending_lookup->task_revision);

  mojom::TaskEffectCompletionPtr approval_surface_completion =
      MakeTaskEffectCompletion(approval_publication.task_effects.front().get(),
                               mojom::TaskEffectCompletionStatus::kSucceeded);
  ASSERT_TRUE(approval_surface_completion);
  CoreResponseBatch approval_surface_completed = core.CompleteTaskEffect(
      approval_publication.task_effects.front().Clone(),
      std::move(approval_surface_completion), kNowMillis + 20u);
  ASSERT_TRUE(approval_surface_completed.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted,
            approval_surface_completed.admission->status);
  // An accepted task change carries exactly one complete state. Empty is the
  // poisoned-runtime path in response_after_task_change, which also refuses
  // the admission and withdraws every effect, so "accepted" and "no state"
  // cannot both be true of one batch.
  EXPECT_EQ(1u, approval_surface_completed.states.size());
  ASSERT_LE(approval_surface_completed.effects.size(), 1u);
  if (!approval_surface_completed.effects.empty()) {
    ASSERT_TRUE(approval_surface_completed.effects.front());
    EXPECT_EQ(mojom::EffectKind::kStorageCommit,
              approval_surface_completed.effects.front()->kind);
    EXPECT_TRUE(approval_surface_completed.effects.front()->storage_commit);
  }

  ledger.Reconcile(approval_bindings, kGeneration, kNowMillis + 21u);
  ledger.RehydrateDurableAuthority(
      *approval_publication.browser_bindings, approval_bindings, kGeneration,
      "browser-session-1", kNowMillis + 21u, kNowUtcMillis,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));

  mojom::CoreServiceCommandPtr approve = test::MakeWorkspaceSnapshotApproval(
      *pending_lookup, pending_approval.action_id);
  ASSERT_EQ(AuthoritySubmissionStage::kStaged,
            ledger.StageSubmittedCommand(*approve, "profile-1",
                                         "browser-session-1", approval_bindings,
                                         kNowMillis + 22u, kNowUtcMillis));
  CoreResponseBatch approve_submitted =
      core.Submit(approve.Clone(), kNowMillis + 22u);
  ASSERT_TRUE(approve_submitted.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted,
            approve_submitted.admission->status);
  ledger.RecordAdmission(approve->operation->operation_id,
                         approve_submitted.admission->status);
  ASSERT_TRUE(approve_submitted.states.empty());
  ASSERT_EQ(1u, approve_submitted.effects.size());
  mojom::EffectEnvelopePtr approve_commit =
      approve_submitted.effects.front().Clone();
  ASSERT_TRUE(approve_commit);
  ASSERT_TRUE(approve_commit->operation);
  ASSERT_TRUE(approve_commit->storage_commit);
  EXPECT_EQ(mojom::StorageOperation::kAppendTaskCommit,
            approve_commit->storage_commit->operation_kind);
  EXPECT_EQ(*approval_revision,
            approve_commit->storage_commit->expected_revision);
  EXPECT_GT(approve_commit->storage_commit->resulting_revision,
            *approval_revision);
  EXPECT_EQ(approve->operation->operation_id, approve_commit->effect_id);
  EXPECT_EQ(approve->operation->operation_id,
            approve_commit->operation->operation_id);
  EXPECT_EQ(approve_commit->storage_commit->resulting_revision,
            approve_commit->operation->task_revision);
  if (approve_commit->storage_commit->workspace) {
    expected_workspace_id =
        approve_commit->storage_commit->workspace->workspace_id;
    expected_workspace_revision =
        approve_commit->storage_commit->workspace->resulting_revision;
    expected_workspace_snapshot =
        approve_commit->storage_commit->workspace->snapshot;
  }
  ASSERT_EQ(AuthorityStorageBinding::kBound,
            ledger.BindStorageCommit(*approve_commit, kNowMillis + 22u));

  mojom::EffectResultPtr approve_commit_result =
      test::DispatchWorkspaceSnapshotEffect(&broker, approve_commit.Clone());
  ++storage_commit_count;
  ASSERT_TRUE(approve_commit_result);
  ASSERT_EQ(mojom::EffectStatus::kCompleted, approve_commit_result->status);
  ASSERT_EQ(AuthorityStorageCompletion::kCommitted,
            ledger.RecordStorageCompletion(*approve_commit_result));
  CoreResponseBatch approved = core.DeliverEffectResult(
      std::move(approve_commit_result), kNowMillis + 23u);
  ASSERT_TRUE(approved.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, approved.admission->status);
  ASSERT_TRUE(approved.effects.empty());
  ASSERT_EQ(1u, approved.states.size());
  CoreStatePublication& approved_publication = approved.states.front();
  ASSERT_TRUE(approved_publication.browser_bindings);
  ASSERT_EQ(1u, approved_publication.task_effects.size());
  const mojom::TaskEffectBinding& approved_effect =
      *approved_publication.task_effects.front();
  ASSERT_EQ(mojom::TaskReducerEffectKind::kAskPolicy, approved_effect.kind);
  ASSERT_TRUE(approved_effect.policy);
  ASSERT_TRUE(approved_effect.policy->approval);
  ASSERT_EQ(
      1u,
      approved_publication.browser_bindings->committed_action_approvals.size());
  const mojom::CommittedActionApprovalBinding& committed_approval =
      *approved_publication.browser_bindings->committed_action_approvals
           .front();
  EXPECT_EQ(approve_commit->storage_commit->resulting_revision,
            committed_approval.committed_revision);
  EXPECT_EQ(approve->user_decision->approval_receipt_id,
            committed_approval.receipt_id);
  EXPECT_EQ(approve->user_decision->approval_digest,
            committed_approval.proposal_digest);
  EXPECT_EQ(committed_approval.committed_revision,
            approved_effect.policy->operation->task_revision);
  EXPECT_EQ(committed_approval.receipt_id,
            approved_effect.policy->approval->receipt_reference);
  EXPECT_EQ(committed_approval.proposal_digest,
            approved_effect.policy->approval->proposal_digest);

  ASSERT_EQ(
      mojom::PendingApprovalRegistrationStatus::kRegistered,
      approval_bindings.Replace(approved_publication.browser_bindings.Clone()));
  ledger.Reconcile(approval_bindings, kGeneration, kNowMillis + 23u);
  ledger.RehydrateDurableAuthority(
      *approved_publication.browser_bindings, approval_bindings, kGeneration,
      "browser-session-1", kNowMillis + 23u, kNowUtcMillis,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));
  EXPECT_TRUE(ledger.ConsumeExactApproval(*approved_effect.policy, kGeneration,
                                          "browser-session-1", kNowMillis + 23u,
                                          kNowUtcMillis));
  EXPECT_FALSE(ledger.ConsumeExactApproval(*approved_effect.policy, kGeneration,
                                           "browser-session-1",
                                           kNowMillis + 23u, kNowUtcMillis));

  mojom::CoreBootstrapPtr restored =
      test::LoadWorkspaceSnapshotBootstrap(&storage);
  ASSERT_TRUE(restored);
  ASSERT_EQ(1u, restored->tasks.size());
  EXPECT_EQ(effect.storage_commit->task_id, restored->tasks[0]->task_id);
  EXPECT_EQ(storage_commit_count, restored->tasks[0]->batches.size());
  ASSERT_EQ(1u, restored->workspaces.size());
  EXPECT_EQ(expected_workspace_id, restored->workspaces[0]->workspace_id);
  EXPECT_EQ(expected_workspace_revision, restored->workspaces[0]->revision);
  EXPECT_EQ(expected_workspace_snapshot, restored->workspaces[0]->snapshot);

  test::VerifyWorkspaceSnapshotTerminalSaveAndRestore(
      &core, &broker, &storage,
      approved_publication.task_effects.front().Clone(), kNowMillis + 24u);
}

}  // namespace
}  // namespace taffy
