// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
constexpr uint64_t kSubmittedRevision = 7u;
constexpr uint64_t kCommittedRevision = 8u;
constexpr uint64_t kNow = 10'000u;
constexpr uint64_t kDeadline = 25'000u;
constexpr uint64_t kExpiry = 20'000u;
constexpr uint64_t kNowUtc = 1'800'000'000'000u;
constexpr uint64_t kExpiryUtc = kNowUtc + 10'000u;
constexpr char kProfileId[] = "profile-1";
constexpr char kBrowserSessionId[] = "browser-session-1";
constexpr char kTaskId[] = "task-1";

std::string Digest() {
  return std::string(64u, 'a');
}

mojom::CoreStateBrowserBindingsPtr State() {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  bindings->task_revisions.push_back(
      mojom::TaskRevisionBinding::New(kTaskId, kGeneration, kSubmittedRevision,
                                      std::vector<mojom::TaskControlKind>()));
  bindings->pending_approvals.push_back(mojom::PendingApprovalBinding::New(
      kTaskId, "action-1", Digest(), kGeneration, kSubmittedRevision));
  return bindings;
}

mojom::CoreServiceCommandPtr AcceptedDecision() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "approve-operation", kGeneration, kSubmittedRevision, kDeadline,
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

mojom::EffectEnvelopePtr StorageEffect(
    const mojom::CoreServiceCommand& command) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = command.operation.Clone();
  effect->operation->task_revision = kCommittedRevision;
  effect->effect_id = command.operation->operation_id;
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = kTaskId;
  effect->storage_commit->expected_revision = kSubmittedRevision;
  effect->storage_commit->resulting_revision = kCommittedRevision;
  return effect;
}

mojom::CoreStateBrowserBindingsPtr CommittedState() {
  auto state = mojom::CoreStateBrowserBindings::New();
  state->service_generation = kGeneration;
  state->state_sequence = 3u;
  state->task_revisions.push_back(
      mojom::TaskRevisionBinding::New(kTaskId, kGeneration, kCommittedRevision,
                                      std::vector<mojom::TaskControlKind>()));
  state->committed_action_approvals.push_back(
      mojom::CommittedActionApprovalBinding::New(
          kTaskId, "action-1", kGeneration, kCommittedRevision, "receipt-1",
          Digest(), kExpiry, kExpiryUtc, kBrowserSessionId));
  return state;
}

mojom::TaskPolicyEffectPtr PolicyEffect() {
  auto effect = mojom::TaskPolicyEffect::New();
  effect->operation = mojom::OperationEnvelope::New(
      "policy-operation", kGeneration, kCommittedRevision, kDeadline,
      "policy-idempotency");
  effect->effect_id = "policy-operation";
  effect->task_id = kTaskId;
  effect->action_id = "action-1";
  effect->proposal_digest = Digest();
  effect->approval =
      mojom::PolicyApprovalFact::New("receipt-1", Digest(), kGeneration,
                                     kExpiry, kExpiryUtc, kBrowserSessionId);
  return effect;
}

class AcceptedApprovalLedgerStorageTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(registry_.Replace(State()),
              mojom::PendingApprovalRegistrationStatus::kRegistered);
    command_ = AcceptedDecision();
    ASSERT_EQ(
        ledger_.StageSubmittedCommand(*command_, kProfileId, kBrowserSessionId,
                                      registry_, kNow, kNowUtc),
        AuthoritySubmissionStage::kStaged);
  }

  CoreStateBindingRegistry registry_;
  AcceptedApprovalLedger ledger_;
  mojom::CoreServiceCommandPtr command_;
};

TEST_F(AcceptedApprovalLedgerStorageTest,
       StorageEnvelopeMustNameTheRevisionItsCommitProduces) {
  auto stale_effect = StorageEffect(*command_);
  stale_effect->operation->task_revision = kSubmittedRevision;
  EXPECT_EQ(ledger_.BindStorageCommit(*stale_effect, kNow),
            AuthorityStorageBinding::kInvalid);

  auto exact_effect = StorageEffect(*command_);
  ASSERT_EQ(ledger_.BindStorageCommit(*exact_effect, kNow),
            AuthorityStorageBinding::kBound);
  auto stale_result = mojom::EffectResult::New();
  stale_result->operation = exact_effect->operation.Clone();
  stale_result->operation->task_revision = kSubmittedRevision;
  stale_result->effect_id = exact_effect->effect_id;
  stale_result->kind = mojom::EffectKind::kStorageCommit;
  stale_result->status = mojom::EffectStatus::kCompleted;
  stale_result->storage = mojom::StorageEffectResult::New(kCommittedRevision);
  EXPECT_EQ(ledger_.RecordStorageCompletion(*stale_result),
            AuthorityStorageCompletion::kRejected);
}

TEST_F(AcceptedApprovalLedgerStorageTest,
       StorageCommitMayBindImmediatelyBeforeDeadline) {
  auto effect = StorageEffect(*command_);
  EXPECT_EQ(ledger_.BindStorageCommit(*effect, kDeadline - 1u),
            AuthorityStorageBinding::kBound);
}

TEST_F(AcceptedApprovalLedgerStorageTest, StorageCommitAtDeadlineFailsClosed) {
  auto effect = StorageEffect(*command_);
  EXPECT_EQ(ledger_.BindStorageCommit(*effect, kDeadline),
            AuthorityStorageBinding::kInvalid);
}

TEST_F(AcceptedApprovalLedgerStorageTest,
       CompletedApprovalSurvivesPreCommitProjection) {
  auto storage = StorageEffect(*command_);
  ASSERT_EQ(ledger_.BindStorageCommit(*storage, kNow),
            AuthorityStorageBinding::kBound);
  auto terminal = mojom::EffectResult::New();
  terminal->operation = storage->operation.Clone();
  terminal->effect_id = storage->effect_id;
  terminal->kind = mojom::EffectKind::kStorageCommit;
  terminal->status = mojom::EffectStatus::kCompleted;
  terminal->storage = mojom::StorageEffectResult::New(kCommittedRevision);
  ASSERT_EQ(ledger_.RecordStorageCompletion(*terminal),
            AuthorityStorageCompletion::kCommitted);

  // Storage and state publication use independent pipes, so a state captured
  // before the commit can arrive after its terminal. The exact submitted
  // revision and pending action retain the browser proof, but cannot use it.
  auto pre_commit = State();
  pre_commit->state_sequence = 2u;
  ASSERT_EQ(registry_.Replace(pre_commit.Clone()),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
  ledger_.Reconcile(registry_, kGeneration, kNow);
  ledger_.RehydrateDurableAuthority(
      *pre_commit, registry_, kGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));
  EXPECT_EQ(ledger_.staged_approval_count_for_testing(), 1u);
  EXPECT_EQ(ledger_.committed_approval_count_for_testing(), 0u);
  EXPECT_FALSE(ledger_.ConsumeExactApproval(*PolicyEffect(), kGeneration,
                                            kBrowserSessionId, kNow, kNowUtc));

  auto committed = CommittedState();
  ASSERT_EQ(registry_.Replace(committed.Clone()),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
  ledger_.Reconcile(registry_, kGeneration, kNow);
  ledger_.RehydrateDurableAuthority(
      *committed, registry_, kGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));
  EXPECT_EQ(ledger_.staged_approval_count_for_testing(), 0u);
  EXPECT_EQ(ledger_.committed_approval_count_for_testing(), 1u);
  EXPECT_TRUE(ledger_.ConsumeExactApproval(*PolicyEffect(), kGeneration,
                                           kBrowserSessionId, kNow, kNowUtc));
  EXPECT_FALSE(ledger_.ConsumeExactApproval(*PolicyEffect(), kGeneration,
                                            kBrowserSessionId, kNow, kNowUtc));
}

}  // namespace
}  // namespace taffy
