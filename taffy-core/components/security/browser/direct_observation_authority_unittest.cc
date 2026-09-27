// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "content/public/test/browser_task_environment.h"
#include "crypto/sha2.h"
#include "taffy/components/security/browser/action_authority.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

class DirectObservationAuthorityTest : public testing::Test {
 protected:
  void SetUp() override {
    leases_.BeginGeneration(kProfileId, kGeneration);
    ledger_.BeginGeneration(kProfileId, kGeneration);
  }

  base::TimeTicks Now() const { return task_environment_.NowTicks(); }

  mojom::MintedCapabilityGrantPtr DirectGrant(
      const ActorLeaseResult& lease,
      std::string subject_id = kDirectIntentId) const {
    auto grant = mojom::MintedCapabilityGrant::New();
    grant->capability_id = "capability-direct";
    grant->service_generation = kGeneration;
    grant->policy_version = 1u;
    grant->actor_lease_id = lease.lease_id.value;
    grant->authority_subject = mojom::AuthoritySubject::New();
    grant->authority_subject->kind =
        mojom::AuthoritySubjectKind::kDirectUserIntent;
    grant->authority_subject->authority_subject_id = std::move(subject_id);
    grant->action_class = mojom::PolicyActionClass::kObservePage;
    grant->operation_kind = mojom::TaskActionOperationKind::kDomRead;
    grant->principal = mojom::PolicyPrincipal::New();
    grant->principal->kind = mojom::PolicyPrincipalKind::kAssistant;
    grant->proposal_digest = std::string(64u, 'a');
    const std::string canonical_digest =
        crypto::SHA256HashString(grant->proposal_digest);
    grant->canonical_intent_digest.assign(canonical_digest.begin(),
                                          canonical_digest.end());
    grant->idempotency_key = "idempotency-direct";
    grant->scope = mojom::PolicyCapabilityScope::New();
    grant->scope->profile_id = kProfileId;
    grant->scope->tab_id = kTabId;
    grant->scope->frame_id = "frame-1";
    grant->scope->page_epoch = "epoch-1";
    grant->scope->origin = mojom::PolicyOrigin::New();
    grant->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
    grant->scope->origin->serialization = kOrigin;
    grant->effective_risk = mojom::PolicyRiskClass::kLocalRead;
    grant->data_classes = {mojom::BipSensitivity::kNotSensitive};
    grant->issued_at_monotonic_ms = ToMonotonicMillis(Now());
    grant->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    return grant;
  }

  mojom::PageObservationEffectPtr DirectEffect() const {
    auto effect = mojom::PageObservationEffect::New();
    effect->authority_subject = mojom::AuthoritySubject::New();
    effect->authority_subject->kind =
        mojom::AuthoritySubjectKind::kDirectUserIntent;
    effect->authority_subject->authority_subject_id = kDirectIntentId;
    effect->tab_id = kTabId;
    effect->frame_id = "frame-1";
    effect->page_epoch = "epoch-1";
    effect->capability_id = "capability-direct";
    effect->proposal_digest = std::string(64u, 'a');
    effect->idempotency_key = "idempotency-direct";
    effect->scope = mojom::ObservationScope::kCurrentDocument;
    effect->max_bytes = mojom::kMaxDirectObservationTotalBytes;
    effect->max_nodes = mojom::kMaxDirectObservationNodes;
    effect->max_text_bytes = mojom::kMaxDirectObservationTextBytes;
    effect->max_frames = mojom::kMaxDirectObservationFrames;
    effect->deadline_ms = mojom::kMaxDirectObservationDeadlineMs;
    effect->expected_graph_revision = 0u;
    return effect;
  }

  static uint64_t ToMonotonicMillis(base::TimeTicks value) {
    const int64_t milliseconds = (value - base::TimeTicks()).InMilliseconds();
    return milliseconds < 0 ? 0u : static_cast<uint64_t>(milliseconds);
  }

  static constexpr uint64_t kGeneration = 7u;
  static constexpr char kProfileId[] = "profile-direct";
  static constexpr char kTabId[] = "tab-direct";
  static constexpr char kDirectIntentId[] = "direct-intent-request-1";
  static constexpr char kOrigin[] = "https://example.test";

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ActorLeaseRegistry leases_;
  CapabilityLedger ledger_;
};

TEST_F(DirectObservationAuthorityTest, DirectCapabilityIsOneUse) {
  const ActorLeaseResult lease = leases_.IssueDirectObservation(
      kDirectIntentId, TabId{kTabId}, mojom::kMaxDirectObservationLeaseMs,
      Now());
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  auto grant = DirectGrant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));
  auto effect = DirectEffect();

  AuthorizedObservationTarget target;
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitObservation(*effect, kOrigin, leases_, Now(),
                                     &target));
  EXPECT_EQ(ObservationScope::kDocument, target.scope);
  EXPECT_FALSE(target.form_root);
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.AdmitObservation(*effect, kOrigin, leases_, Now()));
}

TEST_F(DirectObservationAuthorityTest, NavigationRevokesDirectLease) {
  const ActorLeaseResult lease = leases_.IssueDirectObservation(
      kDirectIntentId, TabId{kTabId}, mojom::kMaxDirectObservationLeaseMs,
      Now());
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_TRUE(leases_.IsValidFor(lease.lease_id, TabId{kTabId}, Now()));

  leases_.PreemptDirectObservations(TabId{kTabId});

  EXPECT_FALSE(leases_.IsValidFor(lease.lease_id, TabId{kTabId}, Now()));
}

TEST_F(DirectObservationAuthorityTest, DirectIdentifierInTaskSlotIsRejected) {
  ActorLeaseRequest request;
  request.task_id = TaskId{kDirectIntentId};
  request.tab_id = TabId{kTabId};
  request.requested_duration_ms = 1'000u;
  const ActorLeaseResult lease = leases_.Issue(request, Now());
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  auto grant = DirectGrant(lease);
  grant->authority_subject->kind = mojom::AuthoritySubjectKind::kTask;
  grant->task_id = kDirectIntentId;
  grant->action_id = "action-1";

  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
            ledger_.Register(*grant, leases_, Now()));
}

TEST_F(DirectObservationAuthorityTest, TaskIdentifierInDirectSlotIsRejected) {
  const ActorLeaseResult lease = leases_.IssueDirectObservation(
      kDirectIntentId, TabId{kTabId}, mojom::kMaxDirectObservationLeaseMs,
      Now());
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  auto grant = DirectGrant(lease, "task-1");

  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
            ledger_.Register(*grant, leases_, Now()));
}

TEST_F(DirectObservationAuthorityTest, LeaseCannotExceedTwoSeconds) {
  EXPECT_EQ(ActorLeaseResultCode::kInternalError,
            leases_
                .IssueDirectObservation(
                    kDirectIntentId, TabId{kTabId},
                    mojom::kMaxDirectObservationLeaseMs + 1u, Now())
                .code);
}

TEST_F(DirectObservationAuthorityTest,
       DirectReadIsRefusedWhileMutatingTaskLeaseIsLive) {
  ActorLeaseRequest mutation;
  mutation.task_id = TaskId{"task-1"};
  mutation.tab_id = TabId{kTabId};
  mutation.requested_duration_ms = 1'000u;
  mutation.mutating = true;
  const ActorLeaseResult mutating_lease = leases_.Issue(mutation, Now());
  ASSERT_EQ(ActorLeaseResultCode::kIssued, mutating_lease.code);
  ASSERT_TRUE(
      leases_.IsValidFor(mutating_lease.lease_id, TabId{kTabId}, Now()));

  const ActorLeaseResult direct_lease = leases_.IssueDirectObservation(
      kDirectIntentId, TabId{kTabId}, mojom::kMaxDirectObservationLeaseMs,
      Now());

  EXPECT_EQ(ActorLeaseResultCode::kAlreadyHeld, direct_lease.code);
  EXPECT_TRUE(
      leases_.IsValidFor(mutating_lease.lease_id, TabId{kTabId}, Now()));
  EXPECT_FALSE(direct_lease.lease_id.is_valid());
}

TEST_F(DirectObservationAuthorityTest,
       MutatingTaskLeaseIsRefusedWhileDirectReadIsLive) {
  const ActorLeaseResult direct_lease = leases_.IssueDirectObservation(
      kDirectIntentId, TabId{kTabId}, mojom::kMaxDirectObservationLeaseMs,
      Now());
  ASSERT_EQ(ActorLeaseResultCode::kIssued, direct_lease.code);

  ActorLeaseRequest mutation;
  mutation.task_id = TaskId{"task-1"};
  mutation.tab_id = TabId{kTabId};
  mutation.requested_duration_ms = 1'000u;
  mutation.mutating = true;

  EXPECT_EQ(ActorLeaseResultCode::kAlreadyHeld,
            leases_.Issue(mutation, Now()).code);
  EXPECT_TRUE(leases_.IsValidFor(direct_lease.lease_id, TabId{kTabId}, Now()));
}

}  // namespace
}  // namespace taffy
