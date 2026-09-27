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

mojom::EffectEnvelopePtr StorageEffect(const mojom::CoreServiceCommand& command,
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
  return effect;
}

void Commit(AcceptedApprovalLedger* ledger,
            const mojom::CoreServiceCommand& command,
            uint64_t resulting_revision) {
  auto effect = StorageEffect(command, resulting_revision);
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

mojom::TaskPolicyEffectPtr PolicyEffect(uint64_t generation) {
  auto effect = mojom::TaskPolicyEffect::New();
  effect->operation = mojom::OperationEnvelope::New(
      "policy-operation", generation, kCommittedRevision, 25'000u,
      "policy-idempotency");
  effect->effect_id = "policy-operation";
  effect->task_id = kTaskId;
  effect->action_id = "action-1";
  effect->tab_id = kTabId;
  effect->proposal_digest = Digest();
  effect->approval = mojom::PolicyApprovalFact::New();
  effect->approval->receipt_reference = "receipt-1";
  effect->approval->proposal_digest = Digest();
  effect->approval->service_generation = generation;
  effect->approval->expires_at_monotonic_ms = kExpiry;
  effect->approval->expires_at_utc_ms = kExpiryUtc;
  effect->approval->browser_session_id = kBrowserSessionId;
  return effect;
}

mojom::CoreStateBrowserBindingsPtr DurableAuthorityState() {
  auto state = State(kCommittedRevision);
  auto consent = mojom::AcceptedTaskConsentBinding::New();
  consent->task_id = kTaskId;
  consent->service_generation = kGeneration;
  consent->current_task_revision = kCommittedRevision;
  consent->accepted_revision = kInitialTaskRevision;
  consent->browser_session_id = kBrowserSessionId;
  consent->receipt_id = "initial-receipt-1";
  consent->consent_preview = StartTask()->start_task->consent_preview.Clone();
  state->accepted_task_consents.push_back(std::move(consent));
  auto approval = mojom::CommittedActionApprovalBinding::New();
  approval->task_id = kTaskId;
  approval->action_id = "action-1";
  approval->service_generation = kGeneration;
  approval->committed_revision = kCommittedRevision;
  approval->receipt_id = "receipt-1";
  approval->proposal_digest = Digest();
  approval->expires_at_monotonic_ms = kExpiry;
  approval->expires_at_utc_ms = kExpiryUtc;
  approval->browser_session_id = kBrowserSessionId;
  state->committed_action_approvals.push_back(std::move(approval));
  return state;
}

void RestampGeneration(mojom::CoreStateBrowserBindings* state,
                       uint64_t generation) {
  state->service_generation = generation;
  for (auto& task : state->task_revisions) {
    task->service_generation = generation;
  }
  for (auto& pending : state->pending_approvals) {
    pending->service_generation = generation;
  }
  for (auto& consent : state->accepted_task_consents) {
    consent->service_generation = generation;
  }
  for (auto& approval : state->committed_action_approvals) {
    approval->service_generation = generation;
  }
}

AuthoritySubmissionStage Stage(AcceptedApprovalLedger* ledger,
                               const mojom::CoreServiceCommand& command,
                               const CoreStateBindingRegistry& registry) {
  return ledger->StageSubmittedCommand(command, kProfileId, kBrowserSessionId,
                                       registry, kNow, kNowUtc);
}

void EstablishDurableAuthority(AcceptedApprovalLedger* ledger) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  auto start = StartTask();
  ASSERT_EQ(Stage(ledger, *start, registry), AuthoritySubmissionStage::kStaged);
  Commit(ledger, *start, kInitialTaskRevision);
  Register(&registry, State(kInitialTaskRevision));
  ledger->Reconcile(registry, kGeneration, kNow);
  Register(&registry, State(kSubmittedRevision, true));
  ledger->Reconcile(registry, kGeneration, kNow);
  auto approval = AcceptedDecision();
  ASSERT_EQ(Stage(ledger, *approval, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(ledger, *approval, kCommittedRevision);
  Register(&registry, State(kCommittedRevision));
  ledger->Reconcile(registry, kGeneration, kNow);
}

void RetainDurableAuthority(AcceptedApprovalLedger* ledger) {
  EstablishDurableAuthority(ledger);
  ledger->ResetForServiceGenerationLoss();
  ASSERT_EQ(ledger->accepted_consent_count_for_testing(), 0u);
  ASSERT_EQ(ledger->committed_approval_count_for_testing(), 0u);
}

void Rehydrate(AcceptedApprovalLedger* ledger,
               CoreStateBindingRegistry* registry,
               mojom::CoreStateBrowserBindingsPtr state,
               const std::string& browser_session_id = kBrowserSessionId) {
  Register(registry, state.Clone());
  ledger->RehydrateDurableAuthority(
      *state, *registry, state->service_generation, browser_session_id, kNow,
      kNowUtc, base::BindRepeating([](const mojom::TaskConsentSource&) {
        return IssuedSourceLiveness::kLive;
      }));
}

TEST(AcceptedApprovalLedgerRehydrationTest,
     ProjectionAloneCannotMintDurableAuthority) {
  CoreStateBindingRegistry registry;
  auto state = DurableAuthorityState();
  AcceptedApprovalLedger ledger;
  Rehydrate(&ledger, &registry, std::move(state));
  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kCommittedRevision, 25'000u, "policy-key");
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                             kOrigin, kGeneration));
  EXPECT_FALSE(ledger.ConsumeExactApproval(*PolicyEffect(kGeneration),
                                           kGeneration, kBrowserSessionId, kNow,
                                           kNowUtc));
}

TEST(AcceptedApprovalLedgerRehydrationTest,
     GenerationLossDiscardsIncompleteStorageCandidates) {
  for (const bool bind_storage : {false, true}) {
    SCOPED_TRACE(bind_storage);
    CoreStateBindingRegistry initial_registry;
    Register(&initial_registry, State(std::nullopt));
    AcceptedApprovalLedger ledger;
    auto start = StartTask();
    ASSERT_EQ(Stage(&ledger, *start, initial_registry),
              AuthoritySubmissionStage::kStaged);
    if (bind_storage) {
      auto effect = StorageEffect(*start, kInitialTaskRevision);
      ASSERT_EQ(ledger.BindStorageCommit(*effect, kNow),
                AuthorityStorageBinding::kBound);
    }
    ledger.ResetForServiceGenerationLoss();

    CoreStateBindingRegistry restart_registry;
    auto projected = DurableAuthorityState();
    RestampGeneration(projected.get(), kRestartGeneration);
    Rehydrate(&ledger, &restart_registry, std::move(projected));
    EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 0u);
    EXPECT_EQ(ledger.committed_approval_count_for_testing(), 0u);
  }
}

TEST(AcceptedApprovalLedgerRehydrationTest,
     FirstProjectionMayAdvancePastAcceptedConsentRevision) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  AcceptedApprovalLedger ledger;
  auto start = StartTask();
  ASSERT_EQ(Stage(&ledger, *start, registry),
            AuthoritySubmissionStage::kStaged);
  Commit(&ledger, *start, kInitialTaskRevision);

  auto projected = DurableAuthorityState();
  projected->committed_action_approvals.clear();
  Register(&registry, projected.Clone());
  ledger.Reconcile(registry, kGeneration, kNow);
  ledger.RehydrateDurableAuthority(
      *projected, registry, kGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));

  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kCommittedRevision, 25'000u, "policy-key");
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kGeneration));
}

TEST(AcceptedApprovalLedgerRehydrationTest,
     RetainedCandidateRefusesCompetingSameKeyCommands) {
  AcceptedApprovalLedger ledger;
  RetainDurableAuthority(&ledger);

  CoreStateBindingRegistry start_registry;
  auto no_tasks = State(std::nullopt);
  RestampGeneration(no_tasks.get(), kRestartGeneration);
  Register(&start_registry, std::move(no_tasks));
  auto competing_start = StartTask();
  competing_start->operation->operation_id = "competing-start";
  competing_start->operation->idempotency_key = "competing-start-key";
  competing_start->operation->service_generation = kRestartGeneration;
  competing_start->start_task->initial_consent_receipt_id =
      "competing-start-receipt";
  EXPECT_EQ(Stage(&ledger, *competing_start, start_registry),
            AuthoritySubmissionStage::kInvalid);

  CoreStateBindingRegistry approval_registry;
  auto pending = State(kSubmittedRevision, true);
  RestampGeneration(pending.get(), kRestartGeneration);
  Register(&approval_registry, std::move(pending));
  auto competing_approval = AcceptedDecision();
  competing_approval->operation->operation_id = "competing-approval";
  competing_approval->operation->idempotency_key = "competing-approval-key";
  competing_approval->operation->service_generation = kRestartGeneration;
  competing_approval->user_decision->approval_receipt_id =
      "competing-approval-receipt";
  EXPECT_EQ(Stage(&ledger, *competing_approval, approval_registry),
            AuthoritySubmissionStage::kInvalid);

  CoreStateBindingRegistry restart_registry;
  auto projected = DurableAuthorityState();
  RestampGeneration(projected.get(), kRestartGeneration);
  Rehydrate(&ledger, &restart_registry, std::move(projected));
  EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 1u);
  EXPECT_EQ(ledger.committed_approval_count_for_testing(), 1u);
}

TEST(AcceptedApprovalLedgerRehydrationTest,
     ExactBrowserOwnedAuthorityRestampsAcrossServiceGenerationLoss) {
  AcceptedApprovalLedger ledger;
  RetainDurableAuthority(&ledger);
  CoreStateBindingRegistry registry;
  auto state = DurableAuthorityState();
  RestampGeneration(state.get(), kRestartGeneration);
  Rehydrate(&ledger, &registry, std::move(state));

  auto operation = mojom::OperationEnvelope::New(
      "policy", kRestartGeneration, kCommittedRevision, 25'000u, "policy-key");
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kRestartGeneration));
  EXPECT_FALSE(ledger.ConsumeExactApproval(*PolicyEffect(kGeneration),
                                           kRestartGeneration,
                                           kBrowserSessionId, kNow, kNowUtc));
  EXPECT_TRUE(ledger.ConsumeExactApproval(*PolicyEffect(kRestartGeneration),
                                          kRestartGeneration, kBrowserSessionId,
                                          kNow, kNowUtc));
}

TEST(AcceptedApprovalLedgerRehydrationTest,
     OmittedAuthorityIsRevokedAndCannotBeProjectedBack) {
  AcceptedApprovalLedger ledger;
  RetainDurableAuthority(&ledger);
  CoreStateBindingRegistry registry;
  auto omitted = State(kCommittedRevision);
  RestampGeneration(omitted.get(), kRestartGeneration);
  Rehydrate(&ledger, &registry, std::move(omitted));
  auto restored = DurableAuthorityState();
  RestampGeneration(restored.get(), kRestartGeneration);
  Rehydrate(&ledger, &registry, std::move(restored));
  EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 0u);
  EXPECT_EQ(ledger.committed_approval_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerRehydrationTest,
     MutatedConsentAndWidenedApprovalCannotRestamp) {
  AcceptedApprovalLedger ledger;
  RetainDurableAuthority(&ledger);
  CoreStateBindingRegistry registry;
  auto state = DurableAuthorityState();
  state->accepted_task_consents.front()
      ->consent_preview->sources.front()
      ->normalized_origin = "https://mutated.test";
  ++state->committed_action_approvals.front()->expires_at_utc_ms;
  RestampGeneration(state.get(), kRestartGeneration);
  Rehydrate(&ledger, &registry, std::move(state));
  EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 0u);
  EXPECT_EQ(ledger.committed_approval_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerRehydrationTest,
     NewBrowserSessionAndDestructiveResetClearCandidates) {
  auto state = DurableAuthorityState();
  RestampGeneration(state.get(), kRestartGeneration);
  CoreStateBindingRegistry registry;
  AcceptedApprovalLedger new_browser;
  RetainDurableAuthority(&new_browser);
  Rehydrate(&new_browser, &registry, state.Clone(), "new-browser-session");
  EXPECT_EQ(new_browser.accepted_consent_count_for_testing(), 0u);
  EXPECT_EQ(new_browser.committed_approval_count_for_testing(), 0u);

  AcceptedApprovalLedger destructive;
  RetainDurableAuthority(&destructive);
  destructive.Reset();
  Rehydrate(&destructive, &registry, std::move(state));
  EXPECT_EQ(destructive.accepted_consent_count_for_testing(), 0u);
  EXPECT_EQ(destructive.committed_approval_count_for_testing(), 0u);
}

}  // namespace
}  // namespace taffy
