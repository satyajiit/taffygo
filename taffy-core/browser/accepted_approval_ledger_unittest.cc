// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/accepted_approval_ledger.h"

#include <optional>
#include <string>
#include <utility>

#include "taffy/browser/core_state_binding_registry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 3u;
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
                                         bool has_pending_approval = false,
                                         bool has_settlement = false,
                                         bool is_terminal = false) {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  if (!revision) {
    return bindings;
  }
  bindings->task_revisions.push_back(mojom::TaskRevisionBinding::New(
      kTaskId, kGeneration, *revision, std::vector<mojom::TaskControlKind>()));
  if (has_pending_approval) {
    bindings->pending_approvals.push_back(mojom::PendingApprovalBinding::New(
        kTaskId, "action-1", Digest(), kGeneration, *revision));
  }
  if (has_settlement) {
    bindings->task_settlements.push_back(mojom::TaskSettlementBinding::New(
        kTaskId, kGeneration, *revision, mojom::TaskSettlementKind::kCancel));
  }
  if (is_terminal) {
    bindings->terminal_tasks.push_back(mojom::TerminalTaskBinding::New(
        kTaskId, kGeneration, *revision, mojom::TerminalTaskKind::kCompleted));
  }
  return bindings;
}

void Register(CoreStateBindingRegistry* registry,
              mojom::CoreStateBrowserBindingsPtr state) {
  ASSERT_EQ(registry->Replace(std::move(state)),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
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
  command->user_decision->trace_id = "trace-1";
  command->user_decision->approval_receipt_id = "receipt-1";
  command->user_decision->approval_expires_at_monotonic_ms = kExpiry;
  command->user_decision->approval_expires_at_utc_ms = kExpiryUtc;
  command->user_decision->browser_session_id = kBrowserSessionId;
  return command;
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

mojom::EffectEnvelopePtr StorageEffect(const mojom::CoreServiceCommand& command,
                                       uint64_t resulting_revision) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = command.operation.Clone();
  // The command is written against the aggregate before the mutation. The
  // storage envelope names the revision produced by that mutation.
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
  return effect;
}

mojom::EffectResultPtr StorageResult(const mojom::EffectEnvelope& effect,
                                     bool committed = true) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->kind = mojom::EffectKind::kStorageCommit;
  result->status = committed ? mojom::EffectStatus::kCompleted
                             : mojom::EffectStatus::kUnavailable;
  result->storage = mojom::StorageEffectResult::New();
  result->storage->committed_revision =
      committed ? effect.storage_commit->resulting_revision : 0u;
  return result;
}

mojom::TaskPolicyEffectPtr PolicyEffect(
    uint64_t revision = kCommittedRevision) {
  auto effect = mojom::TaskPolicyEffect::New();
  effect->operation = mojom::OperationEnvelope::New(
      "policy-operation", kGeneration, revision, 25'000u, "policy-idempotency");
  effect->effect_id = "policy-operation";
  effect->task_id = kTaskId;
  effect->action_id = "action-1";
  effect->tab_id = kTabId;
  effect->proposal_digest = Digest();
  effect->approval = mojom::PolicyApprovalFact::New();
  effect->approval->receipt_reference = "receipt-1";
  effect->approval->proposal_digest = Digest();
  effect->approval->service_generation = kGeneration;
  effect->approval->expires_at_monotonic_ms = kExpiry;
  effect->approval->expires_at_utc_ms = kExpiryUtc;
  effect->approval->browser_session_id = kBrowserSessionId;
  return effect;
}

AuthoritySubmissionStage Stage(AcceptedApprovalLedger* ledger,
                               const mojom::CoreServiceCommand& command,
                               const CoreStateBindingRegistry& registry) {
  return ledger->StageSubmittedCommand(command, kProfileId, kBrowserSessionId,
                                       registry, kNow, kNowUtc);
}

bool Consume(AcceptedApprovalLedger* ledger,
             const mojom::TaskPolicyEffect& effect,
             const std::string& browser_session_id = kBrowserSessionId,
             uint64_t now_utc_ms = kNowUtc) {
  return ledger->ConsumeExactApproval(effect, kGeneration, browser_session_id,
                                      kNow, now_utc_ms);
}

void Commit(AcceptedApprovalLedger* ledger,
            const mojom::CoreServiceCommand& command,
            uint64_t resulting_revision) {
  mojom::EffectEnvelopePtr effect = StorageEffect(command, resulting_revision);
  ASSERT_EQ(ledger->BindStorageCommit(*effect, kNow),
            AuthorityStorageBinding::kBound);
  ASSERT_EQ(ledger->RecordStorageCompletion(*StorageResult(*effect)),
            AuthorityStorageCompletion::kCommitted);
}

TEST(AcceptedApprovalLedgerTest, SubmissionAndAdmissionAreNotAuthority) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(kSubmittedRevision, true));
  AcceptedApprovalLedger ledger;
  auto command = AcceptedDecision();
  ASSERT_EQ(Stage(&ledger, *command, registry),
            AuthoritySubmissionStage::kStaged);
  ledger.RecordAdmission(command->operation->operation_id,
                         mojom::AdmissionStatus::kAccepted);

  EXPECT_FALSE(Consume(&ledger, *PolicyEffect()));
  EXPECT_EQ(ledger.staged_approval_count_for_testing(), 1u);
  EXPECT_EQ(ledger.committed_approval_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerTest,
     ExactStorageAndAcknowledgedStatePromoteOneUseApproval) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(kSubmittedRevision, true));
  AcceptedApprovalLedger ledger;
  auto command = AcceptedDecision();
  ASSERT_EQ(Stage(&ledger, *command, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(&ledger, *command, kCommittedRevision);
  Register(&registry, State(kCommittedRevision));
  ledger.Reconcile(registry, kGeneration, kNow);

  EXPECT_EQ(ledger.staged_approval_count_for_testing(), 0u);
  EXPECT_EQ(ledger.committed_approval_count_for_testing(), 1u);
  EXPECT_TRUE(Consume(&ledger, *PolicyEffect()));
  EXPECT_FALSE(Consume(&ledger, *PolicyEffect()));
}

TEST(AcceptedApprovalLedgerTest,
     FailedAdmissionClearsOnlyAnUncommittedSubmission) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(kSubmittedRevision, true));

  AcceptedApprovalLedger uncommitted;
  auto first = AcceptedDecision();
  ASSERT_EQ(Stage(&uncommitted, *first, registry),
            AuthoritySubmissionStage::kStaged);
  uncommitted.RecordAdmission(first->operation->operation_id,
                              mojom::AdmissionStatus::kInvalidCommand);
  EXPECT_EQ(uncommitted.staged_approval_count_for_testing(), 0u);

  AcceptedApprovalLedger durable;
  auto second = AcceptedDecision();
  ASSERT_EQ(Stage(&durable, *second, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(&durable, *second, kCommittedRevision);
  durable.RecordAdmission(second->operation->operation_id,
                          mojom::AdmissionStatus::kCoreUnavailable);
  Register(&registry, State(kCommittedRevision));
  durable.Reconcile(registry, kGeneration, kNow);
  EXPECT_TRUE(Consume(&durable, *PolicyEffect()));
}

TEST(AcceptedApprovalLedgerTest, MismatchedStorageOrReceiptFailsClosed) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(kSubmittedRevision, true));
  AcceptedApprovalLedger ledger;
  auto command = AcceptedDecision();
  ASSERT_EQ(Stage(&ledger, *command, registry),
            AuthoritySubmissionStage::kStaged);
  auto wrong_effect = StorageEffect(*command, kCommittedRevision);
  wrong_effect->storage_commit->task_id = "different-task";
  EXPECT_EQ(ledger.BindStorageCommit(*wrong_effect, kNow),
            AuthorityStorageBinding::kInvalid);
  EXPECT_EQ(ledger.RecordStorageCompletion(*StorageResult(*wrong_effect)),
            AuthorityStorageCompletion::kRejected);

  AcceptedApprovalLedger forged;
  Register(&registry, State(kSubmittedRevision, true));
  auto valid = AcceptedDecision();
  ASSERT_EQ(Stage(&forged, *valid, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(&forged, *valid, kCommittedRevision);
  Register(&registry, State(kCommittedRevision));
  forged.Reconcile(registry, kGeneration, kNow);
  auto wrong_receipt = PolicyEffect();
  wrong_receipt->approval->receipt_reference = "forged-receipt";
  EXPECT_FALSE(Consume(&forged, *wrong_receipt));
  EXPECT_TRUE(Consume(&forged, *PolicyEffect()));
}

TEST(AcceptedApprovalLedgerTest,
     InitialConsentRequiresDurabilityAndExactLiveSource) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  ASSERT_EQ(Stage(&ledger, *command, registry),
            AuthoritySubmissionStage::kStaged);
  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kInitialTaskRevision, 25'000u, "policy-key");
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                             kOrigin, kGeneration));

  Commit(&ledger, *command, kInitialTaskRevision);
  Register(&registry, State(kInitialTaskRevision));
  ledger.Reconcile(registry, kGeneration, kNow);
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kGeneration));
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(kTaskId, "other-tab", *operation,
                                             kOrigin, kGeneration));
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(
      kTaskId, kTabId, *operation, "https://other.test", kGeneration));

  operation->task_revision = kInitialTaskRevision + 1u;
  Register(&registry, State(operation->task_revision));
  ledger.Reconcile(registry, kGeneration, kNow);
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kGeneration));
}

TEST(AcceptedApprovalLedgerTest,
     DuplicateSourceAndTerminalTaskNeverRetainConsent) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  AcceptedApprovalLedger invalid;
  auto duplicate = StartTask();
  duplicate->start_task->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("source-2", kTabId, kOrigin, std::nullopt));
  EXPECT_EQ(Stage(&invalid, *duplicate, registry),
            AuthoritySubmissionStage::kInvalid);

  AcceptedApprovalLedger terminal;
  auto command = StartTask();
  ASSERT_EQ(Stage(&terminal, *command, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(&terminal, *command, kInitialTaskRevision);
  Register(&registry, State(kInitialTaskRevision));
  terminal.Reconcile(registry, kGeneration, kNow);
  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kInitialTaskRevision, 25'000u, "policy-key");
  ASSERT_TRUE(terminal.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                              kOrigin, kGeneration));
  terminal.RevokeTaskApprovals(kTaskId, kGeneration);
  EXPECT_TRUE(terminal.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                              kOrigin, kGeneration));

  Register(&registry, State(kInitialTaskRevision, false, false, true));
  terminal.Reconcile(registry, kGeneration, kNow);
  EXPECT_FALSE(terminal.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                               kOrigin, kGeneration));
}

}  // namespace
}  // namespace taffy
