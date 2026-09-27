// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <vector>

#include "base/test/task_environment.h"
#include "content/public/test/browser_task_environment.h"
#include "crypto/sha2.h"
#include "taffy/components/security/browser/action_authority.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

class TaskObservationAuthorityTest : public testing::Test {
 protected:
  void SetUp() override {
    leases_.BeginGeneration(kProfileId, kGeneration);
    ledger_.BeginGeneration(kProfileId, kGeneration);
  }

  base::TimeTicks Now() const { return task_environment_.NowTicks(); }

  ActorLeaseResult IssueLease() {
    ActorLeaseRequest request;
    request.task_id = TaskId{kTaskId};
    request.tab_id = TabId{kTabId};
    request.requested_duration_ms = 1'000u;
    request.mutating = false;
    return leases_.Issue(request, Now());
  }

  mojom::MintedCapabilityGrantPtr Grant(const ActorLeaseResult& lease) const {
    auto grant = mojom::MintedCapabilityGrant::New();
    grant->capability_id = kCapabilityId;
    grant->service_generation = kGeneration;
    grant->policy_version = 1u;
    grant->actor_lease_id = lease.lease_id.value;
    grant->task_id = kTaskId;
    grant->action_id = kActionId;
    grant->action_class = mojom::PolicyActionClass::kObservePage;
    grant->operation_kind = mojom::TaskActionOperationKind::kDomRead;
    const auto canonical_digest = crypto::SHA256Hash(CanonicalIntent());
    grant->canonical_intent_digest.assign(canonical_digest.begin(),
                                          canonical_digest.end());
    grant->principal = mojom::PolicyPrincipal::New();
    grant->principal->kind = mojom::PolicyPrincipalKind::kAssistant;
    grant->proposal_digest = std::string(64u, 'a');
    grant->idempotency_key = kIdempotencyKey;
    grant->scope = mojom::PolicyCapabilityScope::New();
    grant->scope->profile_id = kProfileId;
    grant->scope->tab_id = kTabId;
    grant->scope->frame_id = kFrameId;
    grant->scope->page_epoch = kPageEpoch;
    grant->scope->required_graph_revision = kGraphRevision;
    grant->scope->origin = mojom::PolicyOrigin::New();
    grant->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
    grant->scope->origin->serialization = kOrigin;
    grant->data_classes = {mojom::BipSensitivity::kNotSensitive};
    grant->effective_risk = mojom::PolicyRiskClass::kLocalRead;
    grant->issued_at_monotonic_ms = ToMonotonicMillis(Now());
    grant->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    grant->authority_subject = mojom::AuthoritySubject::New();
    grant->authority_subject->kind = mojom::AuthoritySubjectKind::kTask;
    grant->authority_subject->authority_subject_id = kTaskId;
    return grant;
  }

  mojom::PageObservationEffectPtr Effect() const {
    auto effect = mojom::PageObservationEffect::New();
    effect->task_id = kTaskId;
    effect->action_id = kActionId;
    effect->authority_subject = mojom::AuthoritySubject::New();
    effect->authority_subject->kind = mojom::AuthoritySubjectKind::kTask;
    effect->authority_subject->authority_subject_id = kTaskId;
    effect->tab_id = kTabId;
    effect->frame_id = kFrameId;
    effect->page_epoch = kPageEpoch;
    effect->capability_id = kCapabilityId;
    effect->proposal_digest = std::string(64u, 'a');
    effect->idempotency_key = kIdempotencyKey;
    effect->scope = mojom::ObservationScope::kCurrentDocument;
    effect->max_bytes = 4'096u;
    effect->max_nodes = 64u;
    effect->max_text_bytes = 2'048u;
    effect->max_frames = 1u;
    effect->deadline_ms = 1'000u;
    effect->expected_graph_revision = kGraphRevision;
    return effect;
  }

  mojom::TaskActionEffectPtr Action() const {
    auto action = mojom::TaskActionEffect::New();
    action->action_id = kActionId;
    action->proposal_digest = std::string(64u, 'a');
    action->idempotency_key = kIdempotencyKey;
    action->capability_id = kCapabilityId;
    action->document = mojom::TaskFrozenDocument::New(
        kFrameId, kPageEpoch, kGraphRevision, kOrigin, std::nullopt);
    action->executable = mojom::TaskExecutableAction::New();
    action->executable->action_class = mojom::PolicyActionClass::kObservePage;
    action->executable->operation_kind =
        mojom::TaskActionOperationKind::kDomRead;
    action->executable->canonical_intent = CanonicalIntent();
    action->executable->tab_id = kTabId;
    return action;
  }

  static uint64_t ToMonotonicMillis(base::TimeTicks value) {
    const int64_t milliseconds = (value - base::TimeTicks()).InMilliseconds();
    return milliseconds < 0 ? 0u : static_cast<uint64_t>(milliseconds);
  }

  static std::vector<uint8_t> CanonicalIntent() {
    return {0x01u, 0x02u, 0x03u};
  }

  static constexpr uint64_t kGeneration = 9u;
  static constexpr char kProfileId[] = "profile-task";
  static constexpr char kTaskId[] = "task-1";
  static constexpr char kActionId[] = "action-observe-1";
  static constexpr char kTabId[] = "tab-1";
  static constexpr char kFrameId[] = "frame-1";
  static constexpr char kPageEpoch[] = "epoch-1";
  static constexpr char kOrigin[] = "https://example.test";
  static constexpr char kCapabilityId[] = "capability-task-observe";
  static constexpr char kIdempotencyKey[] = "idempotency-task-observe";
  static constexpr uint64_t kGraphRevision = 17u;

  // Now() reads TaskEnvironment::NowTicks(), which exists only under a mock
  // clock — the two neighbouring authority suites already say so. Without
  // MOCK_TIME every test here aborts on the first Now(), before an
  // assertion runs.
  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ActorLeaseRegistry leases_;
  CapabilityLedger ledger_;
};

TEST_F(TaskObservationAuthorityTest,
       RustMintedTaskCapabilityIsSpentBeforeObservation) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = Grant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));
  const auto effect = Effect();

  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitObservation(*effect, kOrigin, leases_, Now()));
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.AdmitObservation(*effect, kOrigin, leases_, Now()));
}

TEST_F(TaskObservationAuthorityTest,
       TypedObservationMustMatchGrantBeforeSharedProjection) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = Grant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  EXPECT_TRUE(ledger_.TaskObservationMatchesRegisteredGrant(*Action()));

  auto wrong_operation = Action();
  wrong_operation->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomQuery;
  EXPECT_FALSE(
      ledger_.TaskObservationMatchesRegisteredGrant(*wrong_operation));

  auto wrong_intent = Action();
  wrong_intent->executable->canonical_intent.push_back(0x04u);
  EXPECT_FALSE(ledger_.TaskObservationMatchesRegisteredGrant(*wrong_intent));

  auto wrong_idempotency = Action();
  wrong_idempotency->idempotency_key = "idempotency-task-other";
  EXPECT_FALSE(
      ledger_.TaskObservationMatchesRegisteredGrant(*wrong_idempotency));
}

TEST_F(TaskObservationAuthorityTest,
       DomQueryGrantProducesOnlyDocumentObservationAuthority) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  auto grant = Grant(lease);
  grant->operation_kind = mojom::TaskActionOperationKind::kDomQuery;
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  auto action = Action();
  action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomQuery;
  EXPECT_TRUE(ledger_.TaskObservationMatchesRegisteredGrant(*action));

  AuthorizedObservationTarget target;
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitObservation(*Effect(), kOrigin, leases_, Now(),
                                     &target));
  EXPECT_EQ(ObservationScope::kDocument, target.scope);
  EXPECT_EQ(AuthorizedObservationKind::kDocument, target.kind);
  EXPECT_FALSE(target.form_root);
  EXPECT_FALSE(target.media_root);
}

TEST_F(TaskObservationAuthorityTest,
       DomQueryGrantCannotCarryAnExecutableNodeScope) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  auto grant = Grant(lease);
  grant->operation_kind = mojom::TaskActionOperationKind::kDomQuery;
  grant->scope->node_id = "node-1";
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
            ledger_.Register(*grant, leases_, Now()));
}

TEST_F(TaskObservationAuthorityTest,
       OperationDerivesExactFormAndSelectionProjection) {
  const ActorLeaseResult form_lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, form_lease.code);
  auto form_grant = Grant(form_lease);
  form_grant->operation_kind =
      mojom::TaskActionOperationKind::kFormInspect;
  form_grant->scope->node_id = "form-1";
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*form_grant, leases_, Now()));

  auto form_action = Action();
  form_action->executable->operation_kind =
      mojom::TaskActionOperationKind::kFormInspect;
  form_action->executable->node_id = "form-1";
  EXPECT_TRUE(ledger_.TaskObservationMatchesRegisteredGrant(*form_action));

  AuthorizedObservationTarget form_target;
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitObservation(*Effect(), kOrigin, leases_, Now(),
                                     &form_target));
  EXPECT_EQ(ObservationScope::kSection, form_target.scope);
  ASSERT_TRUE(form_target.form_root);
  EXPECT_EQ(SemanticNodeId{"form-1"}, form_target.form_root->node_id);
  EXPECT_EQ(kGraphRevision,
            form_target.form_root->minimum_graph_revision);

  // A distinct capability and lease prove SelectionRead cannot inherit the
  // form root merely because both operations share ObservePage on the wire.
  auto selection_grant = Grant(form_lease);
  selection_grant->capability_id = "capability-selection-observe";
  selection_grant->operation_kind =
      mojom::TaskActionOperationKind::kSelectionRead;
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*selection_grant, leases_, Now()));
  auto selection_effect = Effect();
  selection_effect->capability_id = selection_grant->capability_id;
  AuthorizedObservationTarget selection_target;
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitObservation(*selection_effect, kOrigin, leases_,
                                     Now(), &selection_target));
  EXPECT_EQ(ObservationScope::kSelection, selection_target.scope);
  EXPECT_FALSE(selection_target.form_root);
}

TEST_F(TaskObservationAuthorityTest,
       ReidentifiedDocumentOrTaskNeverConsumesCapability) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = Grant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  auto wrong_document = Effect();
  wrong_document->page_epoch = "epoch-other";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitObservation(*wrong_document, kOrigin, leases_, Now()));

  auto wrong_task = Effect();
  wrong_task->authority_subject->authority_subject_id = "task-other";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitObservation(*wrong_task, kOrigin, leases_, Now()));

  auto wrong_graph = Effect();
  wrong_graph->expected_graph_revision = kGraphRevision + 1u;
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitObservation(*wrong_graph, kOrigin, leases_, Now()));

  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitObservation(*Effect(), kOrigin, leases_, Now()));
}

TEST_F(TaskObservationAuthorityTest, TaskSettlementRevokesCapabilityAndLease) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = Grant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  ledger_.RevokeTask(kTaskId, kGeneration);
  leases_.RevokeTask(kTaskId, kGeneration);

  EXPECT_EQ(CapabilityAdmission::kMalformed,
            ledger_.AdmitObservation(*Effect(), kOrigin, leases_, Now()));
  EXPECT_FALSE(leases_.IsValidFor(lease.lease_id, TabId{kTabId}, Now()));
}

}  // namespace
}  // namespace taffy
