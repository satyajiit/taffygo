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
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 3u;
constexpr uint64_t kRestartGeneration = kGeneration + 1u;
constexpr uint64_t kSubmittedRevision = 7u;
constexpr uint64_t kCommittedRevision = 8u;
constexpr uint64_t kInitialTaskRevision = 3u;
constexpr uint64_t kNow = 10'000u;
constexpr uint64_t kExpiry = 20'000u;
constexpr uint64_t kNowUtc = 1'800'000'000'000u;
constexpr uint64_t kExpiryUtc = kNowUtc + 10'000u;
constexpr char kProfileId[] = "profile-1";
constexpr char kBrowserSessionId[] = "browser-session-1";
constexpr char kTaskId[] = "task-1";
constexpr char kTabId[] = "tab-1";
constexpr char kOrigin[] = "https://example.test";

std::string Digest() {
  return std::string(64u, 'a');
}

mojom::CoreStateBrowserBindingsPtr State(std::optional<uint64_t> revision,
                                         bool pending_approval = false) {
  auto state = mojom::CoreStateBrowserBindings::New();
  state->service_generation = kGeneration;
  state->state_sequence = 1u;
  if (revision) {
    state->task_revisions.push_back(
        mojom::TaskRevisionBinding::New(kTaskId, kGeneration, *revision,
                                        std::vector<mojom::TaskControlKind>()));
  }
  if (revision && pending_approval) {
    state->pending_approvals.push_back(mojom::PendingApprovalBinding::New(
        kTaskId, "action-1", Digest(), kGeneration, *revision));
  }
  return state;
}

void Register(CoreStateBindingRegistry* registry,
              mojom::CoreStateBrowserBindingsPtr state) {
  ASSERT_EQ(registry->Replace(std::move(state)),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
}

mojom::CoreServiceCommandPtr StartTask() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "start-operation", kGeneration, 0u, 25'000u, "start-idempotency");
  command->kind = mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = mojom::StartTaskCommand::New();
  command->start_task->task_id = kTaskId;
  command->start_task->browser_profile_id = kProfileId;
  command->start_task->browser_session_id = kBrowserSessionId;
  command->start_task->template_id = mojom::TaskTemplateId::kBuildSourceTable;
  command->start_task->provider_route_id = "no_model_required";
  command->start_task->tool_allowlist = {"browser.dom.read"};
  command->start_task->initial_consent_receipt_id = "initial-receipt-1";
  command->start_task->consent_preview = mojom::TaskConsentPreview::New();
  command->start_task->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("source-1", kTabId, kOrigin, std::nullopt));
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kNoModelRequired;
  return command;
}

mojom::CoreServiceCommandPtr AcceptedDecision() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "approve-operation", kGeneration, kSubmittedRevision, 25'000u,
      "approve-idempotency");
  command->kind = mojom::CoreServiceCommandKind::kUserDecision;
  command->user_decision = mojom::UserDecisionCommand::New();
  command->user_decision->task_id = kTaskId;
  command->user_decision->action_id = "action-1";
  command->user_decision->decision = mojom::UserDecisionKind::kAccept;
  command->user_decision->approval_digest = Digest();
  command->user_decision->approval_receipt_id = "receipt-1";
  command->user_decision->approval_expires_at_monotonic_ms = kExpiry;
  command->user_decision->approval_expires_at_utc_ms = kExpiryUtc;
  command->user_decision->browser_session_id = kBrowserSessionId;
  return command;
}

mojom::CoreServiceCommandPtr ResumeTask(uint64_t revision,
                                        uint64_t generation = kGeneration) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "resume-operation", generation, revision, 25'000u, "resume-idempotency");
  command->kind = mojom::CoreServiceCommandKind::kResumeTask;
  command->resume_task = mojom::ResumeTaskCommand::New(kTaskId, "resume-trace");
  return command;
}

void Commit(AcceptedApprovalLedger* ledger,
            const mojom::CoreServiceCommand& command,
            uint64_t resulting_revision) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = command.operation.Clone();
  effect->operation->task_revision = resulting_revision;
  effect->effect_id = command.operation->operation_id;
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = kTaskId;
  effect->storage_commit->expected_revision = command.operation->task_revision;
  effect->storage_commit->resulting_revision = resulting_revision;
  ASSERT_EQ(ledger->BindStorageCommit(*effect, kNow),
            AuthorityStorageBinding::kBound);
  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->kind = mojom::EffectKind::kStorageCommit;
  result->status = mojom::EffectStatus::kCompleted;
  result->storage = mojom::StorageEffectResult::New(resulting_revision);
  ASSERT_EQ(ledger->RecordStorageCompletion(*result),
            AuthorityStorageCompletion::kCommitted);
}

mojom::CoreStateBrowserBindingsPtr DurableAuthorityState(
    uint64_t current_revision,
    bool pending_approval = false,
    bool committed_approval = false) {
  auto state = State(current_revision, pending_approval);
  auto consent = mojom::AcceptedTaskConsentBinding::New();
  consent->task_id = kTaskId;
  consent->service_generation = kGeneration;
  consent->current_task_revision = current_revision;
  consent->accepted_revision = kInitialTaskRevision;
  consent->browser_session_id = kBrowserSessionId;
  consent->receipt_id = "initial-receipt-1";
  consent->consent_preview = StartTask()->start_task->consent_preview.Clone();
  state->accepted_task_consents.push_back(std::move(consent));
  if (committed_approval) {
    state->committed_action_approvals.push_back(
        mojom::CommittedActionApprovalBinding::New(
            kTaskId, "action-1", kGeneration, current_revision, "receipt-1",
            Digest(), kExpiry, kExpiryUtc, kBrowserSessionId));
  }
  return state;
}

mojom::CoreStateBrowserBindingsPtr PausingState(uint64_t revision) {
  auto state = State(revision);
  state->task_settlements.push_back(mojom::TaskSettlementBinding::New(
      kTaskId, kGeneration, revision, mojom::TaskSettlementKind::kPause));
  return state;
}

mojom::CoreStateBrowserBindingsPtr PausedState(uint64_t revision) {
  auto state = State(revision);
  state->task_revisions.front()->allowed_controls = {
      mojom::TaskControlKind::kResume, mojom::TaskControlKind::kStop};
  return state;
}

mojom::CoreStateBrowserBindingsPtr ResumedAuthorityState(uint64_t revision) {
  return DurableAuthorityState(revision);
}

void RestampGeneration(mojom::CoreStateBrowserBindings* state,
                       uint64_t generation) {
  state->service_generation = generation;
  for (auto& task : state->task_revisions) {
    task->service_generation = generation;
  }
  for (auto& consent : state->accepted_task_consents) {
    consent->service_generation = generation;
  }
}

AuthoritySubmissionStage Stage(AcceptedApprovalLedger* ledger,
                               const mojom::CoreServiceCommand& command,
                               const CoreStateBindingRegistry& registry,
                               uint64_t now_monotonic_ms = kNow) {
  return ledger->StageSubmittedCommand(command, kProfileId, kBrowserSessionId,
                                       registry, now_monotonic_ms, kNowUtc);
}

void ReconcileAndRehydrate(AcceptedApprovalLedger* ledger,
                           CoreStateBindingRegistry* registry,
                           mojom::CoreStateBrowserBindingsPtr state) {
  Register(registry, state.Clone());
  ledger->Reconcile(*registry, state->service_generation, kNow);
  ledger->RehydrateDurableAuthority(
      *state, *registry, state->service_generation, kBrowserSessionId, kNow,
      kNowUtc, base::BindRepeating([](const mojom::TaskConsentSource&) {
        return IssuedSourceLiveness::kLive;
      }));
}

void EstablishDurableAuthority(AcceptedApprovalLedger* ledger) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  auto start = StartTask();
  ASSERT_EQ(Stage(ledger, *start, registry), AuthoritySubmissionStage::kStaged);
  Commit(ledger, *start, kInitialTaskRevision);
  ReconcileAndRehydrate(ledger, &registry,
                        DurableAuthorityState(kInitialTaskRevision));
  ReconcileAndRehydrate(
      ledger, &registry,
      DurableAuthorityState(kSubmittedRevision, /*pending_approval=*/true));
  auto approval = AcceptedDecision();
  ASSERT_EQ(Stage(ledger, *approval, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(ledger, *approval, kCommittedRevision);
  ReconcileAndRehydrate(
      ledger, &registry,
      DurableAuthorityState(kCommittedRevision, /*pending_approval=*/false,
                            /*committed_approval=*/true));
}

TEST(AcceptedApprovalLedgerPauseTest,
     PauseIsInertAndOnlyAnExactDurableResumeRestoresConsent) {
  AcceptedApprovalLedger ledger;
  EstablishDurableAuthority(&ledger);
  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kCommittedRevision, 25'000u, "policy-key");
  ASSERT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kGeneration));

  CoreStateBindingRegistry registry;
  ReconcileAndRehydrate(&ledger, &registry, PausingState(9u));
  EXPECT_EQ(0u, ledger.accepted_consent_count_for_testing());
  EXPECT_EQ(1u, ledger.suspended_consent_count_for_testing());
  EXPECT_EQ(0u, ledger.committed_approval_count_for_testing());
  operation->task_revision = 9u;
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                             kOrigin, kGeneration));

  ReconcileAndRehydrate(&ledger, &registry, PausedState(10u));
  EXPECT_EQ(0u, ledger.accepted_consent_count_for_testing());
  EXPECT_EQ(1u, ledger.suspended_consent_count_for_testing());

  // Even the exact original receipt and source projection cannot turn a held
  // task live. The browser must first own a durable Resume operation.
  auto projection_alone = ResumedAuthorityState(10u);
  projection_alone->task_revisions.front()->allowed_controls = {
      mojom::TaskControlKind::kResume, mojom::TaskControlKind::kStop};
  ReconcileAndRehydrate(&ledger, &registry, std::move(projection_alone));
  EXPECT_EQ(0u, ledger.accepted_consent_count_for_testing());
  EXPECT_EQ(1u, ledger.suspended_consent_count_for_testing());

  auto resume = ResumeTask(10u);
  ASSERT_EQ(Stage(&ledger, *resume, registry),
            AuthoritySubmissionStage::kStaged);
  EXPECT_EQ(1u, ledger.staged_resume_count_for_testing());
  Commit(&ledger, *resume, 11u);

  // A state captured before the commit may cross the publication pipe after
  // its terminal. It keeps the durable Resume arm inert until revision 11.
  ReconcileAndRehydrate(&ledger, &registry, PausedState(10u));
  EXPECT_EQ(1u, ledger.staged_resume_count_for_testing());

  // The core may finish more durable setup before its first post-Resume state.
  ReconcileAndRehydrate(&ledger, &registry, ResumedAuthorityState(12u));
  EXPECT_EQ(1u, ledger.accepted_consent_count_for_testing());
  EXPECT_EQ(0u, ledger.suspended_consent_count_for_testing());
  EXPECT_EQ(0u, ledger.staged_resume_count_for_testing());
  EXPECT_EQ(0u, ledger.committed_approval_count_for_testing());
  operation->task_revision = 12u;
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kGeneration));
}

TEST(AcceptedApprovalLedgerPauseTest,
     CommittedResumeClosesIfReachedStateOmitsConsent) {
  AcceptedApprovalLedger ledger;
  EstablishDurableAuthority(&ledger);
  CoreStateBindingRegistry registry;
  ReconcileAndRehydrate(&ledger, &registry, PausingState(9u));
  ReconcileAndRehydrate(&ledger, &registry, PausedState(10u));
  auto resume = ResumeTask(10u);
  ASSERT_EQ(Stage(&ledger, *resume, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(&ledger, *resume, 11u);

  ReconcileAndRehydrate(&ledger, &registry, State(11u));
  ASSERT_EQ(0u, ledger.suspended_consent_count_for_testing());
  ReconcileAndRehydrate(&ledger, &registry, ResumedAuthorityState(12u));
  EXPECT_EQ(0u, ledger.accepted_consent_count_for_testing());
}

TEST(AcceptedApprovalLedgerPauseTest,
     CancelSettlementDestroysSuspendedConsent) {
  AcceptedApprovalLedger ledger;
  EstablishDurableAuthority(&ledger);
  CoreStateBindingRegistry registry;
  ReconcileAndRehydrate(&ledger, &registry, PausingState(9u));
  ASSERT_EQ(1u, ledger.suspended_consent_count_for_testing());

  auto cancelling = State(10u);
  cancelling->task_settlements.push_back(mojom::TaskSettlementBinding::New(
      kTaskId, kGeneration, 10u, mojom::TaskSettlementKind::kCancel));
  ReconcileAndRehydrate(&ledger, &registry, std::move(cancelling));
  EXPECT_EQ(0u, ledger.accepted_consent_count_for_testing());
  EXPECT_EQ(0u, ledger.suspended_consent_count_for_testing());
}

TEST(AcceptedApprovalLedgerPauseTest,
     SuspendedConsentAndCommittedResumeSurviveServiceGenerationLoss) {
  AcceptedApprovalLedger ledger;
  EstablishDurableAuthority(&ledger);
  CoreStateBindingRegistry registry;
  ReconcileAndRehydrate(&ledger, &registry, PausingState(9u));
  ledger.ResetForServiceGenerationLoss();
  auto restarted_paused = PausedState(10u);
  RestampGeneration(restarted_paused.get(), kRestartGeneration);
  ReconcileAndRehydrate(&ledger, &registry, std::move(restarted_paused));

  auto resume = ResumeTask(10u, kRestartGeneration);
  ASSERT_EQ(Stage(&ledger, *resume, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(&ledger, *resume, 11u);
  ledger.ResetForServiceGenerationLoss();
  ASSERT_EQ(1u, ledger.staged_resume_count_for_testing());

  constexpr uint64_t kSecondRestartGeneration = kRestartGeneration + 1u;
  auto resumed = ResumedAuthorityState(12u);
  RestampGeneration(resumed.get(), kSecondRestartGeneration);
  ReconcileAndRehydrate(&ledger, &registry, std::move(resumed));
  auto operation = mojom::OperationEnvelope::New(
      "policy", kSecondRestartGeneration, 12u, 25'000u, "policy-key");
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kSecondRestartGeneration));
  EXPECT_EQ(0u, ledger.suspended_consent_count_for_testing());
}

TEST(AcceptedApprovalLedgerPauseTest,
     RestartingDirectlyPausedStillRetainsInertConsentForResume) {
  AcceptedApprovalLedger ledger;
  EstablishDurableAuthority(&ledger);

  // The service dies after it durably settled Pause but before its Pausing
  // snapshot crossed the publication pipe. Recovery therefore starts at the
  // final Paused state, without a settlement binding for the browser to see.
  ledger.ResetForServiceGenerationLoss();
  CoreStateBindingRegistry registry;
  auto restarted_paused = PausedState(10u);
  RestampGeneration(restarted_paused.get(), kRestartGeneration);
  ReconcileAndRehydrate(&ledger, &registry, std::move(restarted_paused));
  ASSERT_EQ(0u, ledger.accepted_consent_count_for_testing());
  ASSERT_EQ(1u, ledger.suspended_consent_count_for_testing());

  auto resume = ResumeTask(10u, kRestartGeneration);
  ASSERT_EQ(Stage(&ledger, *resume, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(&ledger, *resume, 11u);
  auto resumed = ResumedAuthorityState(11u);
  RestampGeneration(resumed.get(), kRestartGeneration);
  ReconcileAndRehydrate(&ledger, &registry, std::move(resumed));
  EXPECT_EQ(1u, ledger.accepted_consent_count_for_testing());
  EXPECT_EQ(0u, ledger.suspended_consent_count_for_testing());
}

TEST(AcceptedApprovalLedgerPauseTest,
     GenerationLossDiscardsIncompleteResumeArm) {
  AcceptedApprovalLedger ledger;
  EstablishDurableAuthority(&ledger);
  CoreStateBindingRegistry registry;
  ReconcileAndRehydrate(&ledger, &registry, PausingState(9u));
  ReconcileAndRehydrate(&ledger, &registry, PausedState(10u));
  auto resume = ResumeTask(10u);
  ASSERT_EQ(Stage(&ledger, *resume, registry),
            AuthoritySubmissionStage::kStaged);

  ledger.ResetForServiceGenerationLoss();
  EXPECT_EQ(0u, ledger.staged_resume_count_for_testing());
  auto forged = ResumedAuthorityState(11u);
  RestampGeneration(forged.get(), kRestartGeneration);
  ReconcileAndRehydrate(&ledger, &registry, std::move(forged));
  EXPECT_EQ(0u, ledger.accepted_consent_count_for_testing());
  EXPECT_EQ(1u, ledger.suspended_consent_count_for_testing());
}

TEST(AcceptedApprovalLedgerPauseTest,
     ExpiredUncommittedResumeDoesNotBlockRetry) {
  AcceptedApprovalLedger ledger;
  EstablishDurableAuthority(&ledger);
  CoreStateBindingRegistry registry;
  ReconcileAndRehydrate(&ledger, &registry, PausingState(9u));
  ReconcileAndRehydrate(&ledger, &registry, PausedState(10u));
  auto expired = ResumeTask(10u);
  ASSERT_EQ(Stage(&ledger, *expired, registry),
            AuthoritySubmissionStage::kStaged);

  auto retry = ResumeTask(10u);
  retry->operation->operation_id = "resume-retry";
  retry->operation->idempotency_key = "resume-retry-key";
  retry->operation->deadline_monotonic_ms = 35'000u;
  EXPECT_EQ(Stage(&ledger, *retry, registry, 25'000u),
            AuthoritySubmissionStage::kStaged);
  EXPECT_EQ(1u, ledger.staged_resume_count_for_testing());
}

}  // namespace
}  // namespace taffy
