// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/security/browser/action_authority.h"

#include <algorithm>

#include "base/test/task_environment.h"
#include "content/public/test/browser_task_environment.h"
#include "crypto/sha2.h"
#include "testing/gtest/include/gtest/gtest.h"

// The invariants these tests hold down:
//
//   * a capability is one use, and a failed use still spends it;
//   * an expired capability is never admitted;
//   * an envelope edited after authorization does not hash to its digest;
//   * a lease belongs to exactly one tab;
//   * at most one mutating lease exists per tab;
//   * direct user input revokes every lease on the tab;
//   * a handover revokes before it counts, counts only inside its own window
//     and its own tab, saturates rather than recording how much was typed,
//     refuses mutation authority over the tab for as long as it is open, and
//     refuses to hand back the authority it took away.

namespace taffy {
namespace {

ContentDigest MakeDigest(const std::string& value) {
  ContentDigest digest;
  digest.algorithm = DigestAlgorithm::kSha256;
  digest.value = value;
  return digest;
}

MonotonicMillis ToMonotonicMs(base::TimeTicks ticks) {
  return static_cast<MonotonicMillis>(
      (ticks - base::TimeTicks()).InMilliseconds());
}

class ActionAuthorityTest : public testing::Test {
 protected:
  void SetUp() override {
    leases_.BeginGeneration("profile_1", 1u);
    ledger_.BeginGeneration("profile_1", 1u);
  }

  ActorLeaseId IssueLease(const TabId& tab_id, bool mutating) {
    ActorLeaseRequest request;
    request.task_id = TaskId{"task_1"};
    request.tab_id = tab_id;
    request.mutating = mutating;
    request.requested_duration_ms = 1000;
    return leases_.Issue(request, Now()).lease_id;
  }

  CapabilityGrant MakeGrant(const ActorLeaseId& lease_id,
                            const std::string& reference,
                            const ContentDigest& digest) {
    CapabilityGrant grant;
    grant.capability_reference = CapabilityReference{reference};
    grant.actor_lease_id = lease_id;
    grant.expires_at_monotonic_ms = ToMonotonicMs(Now()) + 900u;
    grant.policy_version = 1u;

    auto wire = core_service::mojom::MintedCapabilityGrant::New();
    wire->capability_id = reference;
    wire->service_generation = 1u;
    wire->policy_version = grant.policy_version;
    wire->actor_lease_id = lease_id.value;
    wire->task_id = "task_1";
    wire->action_id = "action_1";
    wire->authority_subject =
        core_service::mojom::AuthoritySubject::New();
    wire->authority_subject->kind =
        core_service::mojom::AuthoritySubjectKind::kTask;
    wire->authority_subject->authority_subject_id = wire->task_id;
    wire->action_class = core_service::mojom::PolicyActionClass::kObservePage;
    wire->operation_kind =
        core_service::mojom::TaskActionOperationKind::kDomRead;
    wire->principal = core_service::mojom::PolicyPrincipal::New();
    wire->principal->kind =
        core_service::mojom::PolicyPrincipalKind::kAssistant;
    wire->proposal_digest = digest.value;
    const std::string canonical_intent_digest =
        crypto::SHA256HashString(wire->proposal_digest);
    wire->canonical_intent_digest.assign(canonical_intent_digest.begin(),
                                         canonical_intent_digest.end());
    wire->idempotency_key = "idempotency_1";
    wire->scope = core_service::mojom::PolicyCapabilityScope::New();
    wire->scope->profile_id = "profile_1";
    wire->scope->tab_id = tab_.value;
    wire->scope->frame_id = "frame_1";
    wire->scope->page_epoch = "epoch_1";
    // A task-subject grant is pinned to the graph revision it was authorized
    // against; the ledger refuses one that is not.
    wire->scope->required_graph_revision = kGraphRevision;
    wire->scope->origin = core_service::mojom::PolicyOrigin::New();
    wire->scope->origin->kind =
        core_service::mojom::PolicyOriginKind::kTuple;
    wire->scope->origin->serialization = "https://example.test";
    wire->effective_risk = core_service::mojom::PolicyRiskClass::kLocalRead;
    wire->issued_at_monotonic_ms = ToMonotonicMs(Now());
    wire->expires_at_monotonic_ms = grant.expires_at_monotonic_ms;
    EXPECT_EQ(core_service::mojom::CapabilityRegistrationStatus::kRegistered,
              ledger_.Register(*wire, leases_, Now()));
    return grant;
  }

  base::TimeTicks Now() const { return task_environment_.NowTicks(); }

  static constexpr uint64_t kGraphRevision = 17u;

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ActorLeaseRegistry leases_;
  CapabilityLedger ledger_;
  const TabId tab_{"tab_1"};
};

TEST_F(ActionAuthorityTest, CapabilityIsAdmittedExactlyOnce) {
  const ActorLeaseId lease = IssueLease(tab_, /*mutating=*/true);
  const ContentDigest digest = MakeDigest(std::string(64u, 'a'));
  const CapabilityGrant grant = MakeGrant(lease, "cap_1", digest);

  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.Admit(grant, digest, digest, leases_, tab_, Now()));
  // Second use, same capability, same everything.
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.Admit(grant, digest, digest, leases_, tab_, Now()));
}

TEST_F(ActionAuthorityTest, AFailedActionStillSpendsItsCapability) {
  const ActorLeaseId lease = IssueLease(tab_, /*mutating=*/true);
  const ContentDigest digest = MakeDigest(std::string(64u, 'a'));
  const CapabilityGrant grant = MakeGrant(lease, "cap_2", digest);

  ASSERT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.Admit(grant, digest, digest, leases_, tab_, Now()));
  ledger_.Settle(grant.capability_reference, ActionResultCode::kNodeGone);

  // The browser cannot prove the first attempt had no effect, so the
  // capability stays spent.
  EXPECT_TRUE(ledger_.IsSpent(grant.capability_reference));
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.Admit(grant, digest, digest, leases_, tab_, Now()));
}

TEST_F(ActionAuthorityTest, ExpiredCapabilityIsNeverAdmitted) {
  const ActorLeaseId lease = IssueLease(tab_, /*mutating=*/true);
  const ContentDigest digest = MakeDigest(std::string(64u, 'a'));
  const CapabilityGrant grant = MakeGrant(lease, "cap_3", digest);

  task_environment_.FastForwardBy(base::Seconds(5));
  EXPECT_EQ(CapabilityAdmission::kExpired,
            ledger_.Admit(grant, digest, digest, leases_, tab_, Now()));
}

TEST_F(ActionAuthorityTest, EditedEnvelopeFailsTheDigestCheck) {
  const ActorLeaseId lease = IssueLease(tab_, /*mutating=*/true);
  const ContentDigest authorized = MakeDigest(std::string(64u, 'a'));
  const CapabilityGrant grant = MakeGrant(lease, "cap_4", authorized);

  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.Admit(grant, authorized, MakeDigest(std::string(64u, 'b')),
                          leases_, tab_, Now()));
}

TEST_F(ActionAuthorityTest, CapabilityFromAnotherTabIsRefused) {
  const ActorLeaseId lease = IssueLease(tab_, /*mutating=*/true);
  const ContentDigest digest = MakeDigest(std::string(64u, 'a'));
  const CapabilityGrant grant = MakeGrant(lease, "cap_5", digest);

  EXPECT_EQ(CapabilityAdmission::kLeaseNotForThisTab,
            ledger_.Admit(grant, digest, digest, leases_, TabId{"tab_other"},
                          Now()));
}

TEST_F(ActionAuthorityTest, AtMostOneMutatingLeasePerTab) {
  ASSERT_TRUE(IssueLease(tab_, /*mutating=*/true).is_valid());

  ActorLeaseRequest second;
  second.task_id = TaskId{"task_2"};
  second.tab_id = tab_;
  second.mutating = true;
  second.requested_duration_ms = 1000;
  EXPECT_EQ(ActorLeaseResultCode::kAlreadyHeld,
            leases_.Issue(second, Now()).code);

  // A non-mutating lease is not exclusive.
  second.mutating = false;
  EXPECT_EQ(ActorLeaseResultCode::kIssued, leases_.Issue(second, Now()).code);
}

TEST_F(ActionAuthorityTest, UserInputPreemptsEveryLeaseOnTheTab) {
  const ActorLeaseId mutating = IssueLease(tab_, /*mutating=*/true);
  const ActorLeaseId reading = IssueLease(tab_, /*mutating=*/false);
  const TabId other{"tab_other"};
  const ActorLeaseId untouched = IssueLease(other, /*mutating=*/true);

  const std::vector<ActorLeaseId> revoked = leases_.PreemptTab(tab_);
  EXPECT_EQ(2u, revoked.size());
  EXPECT_FALSE(leases_.IsValidFor(mutating, tab_, Now()));
  EXPECT_FALSE(leases_.IsValidFor(reading, tab_, Now()));
  // Preemption is per tab: another tab's work is untouched.
  EXPECT_TRUE(leases_.IsValidFor(untouched, other, Now()));
}

TEST_F(ActionAuthorityTest, ExpiredLeaseStopsBeingValid) {
  const ActorLeaseId lease = IssueLease(tab_, /*mutating=*/true);
  ASSERT_TRUE(leases_.IsValidFor(lease, tab_, Now()));
  task_environment_.FastForwardBy(base::Seconds(5));
  EXPECT_FALSE(leases_.IsValidFor(lease, tab_, Now()));
}

TEST_F(ActionAuthorityTest, EveryAdmissionFailureMapsToAFailClosedCode) {
  // No admission failure may map to kVerified, and none may map to a code that
  // reads as success anywhere downstream.
  for (uint8_t raw = 1; raw <= static_cast<uint8_t>(CapabilityAdmission::kMalformed);
       ++raw) {
    const ActionResultCode code =
        AdmissionToResultCode(static_cast<CapabilityAdmission>(raw));
    EXPECT_FALSE(IsActionSuccess(code));
  }
}

// --- Handover ---------------------------------------------------------------

TEST_F(ActionAuthorityTest, HandoverRevokesTheTabsAuthorityBeforeItCounts) {
  const ActorLeaseId mutating = IssueLease(tab_, /*mutating=*/true);
  const ActorLeaseId reading = IssueLease(tab_, /*mutating=*/false);

  const std::vector<ActorLeaseId> revoked = leases_.BeginHandover(tab_);

  // The person is about to type into this tab and the assistant holds nothing
  // over it. Both halves are checked, because a revocation that returned the
  // identities without invalidating them would pass a weaker test.
  EXPECT_EQ(2u, revoked.size());
  EXPECT_FALSE(leases_.IsValidFor(mutating, tab_, Now()));
  EXPECT_FALSE(leases_.IsValidFor(reading, tab_, Now()));
  EXPECT_FALSE(leases_.HasMutatingLease(tab_, Now()));
  EXPECT_EQ(0u, leases_.HandoverInputCount(tab_));
}

TEST_F(ActionAuthorityTest, HandoverRecordsLeasesAlreadyRevokedOnTheTab) {
  const ActorLeaseId mutating = IssueLease(tab_, /*mutating=*/true);
  leases_.RevokeTask("task_1", 1u);
  EXPECT_FALSE(leases_.IsValidFor(mutating, tab_, Now()));

  const std::vector<ActorLeaseId> just_revoked = leases_.BeginHandover(tab_);
  EXPECT_TRUE(just_revoked.empty());
  EXPECT_EQ(mutating, leases_.FirstHandoverRevokedLease(tab_));

  const HandoverEvidence evidence = leases_.EndHandover(tab_);
  ASSERT_EQ(1u, evidence.revoked.size());
  EXPECT_EQ(mutating, evidence.revoked[0]);
}

TEST_F(ActionAuthorityTest, InputIsCountedOnlyInsideAnOpenWindow) {
  // Before: there is nothing for this to be evidence of.
  leases_.NoteHandoverInput(tab_);
  EXPECT_EQ(0u, leases_.HandoverInputCount(tab_));

  leases_.BeginHandover(tab_);
  leases_.NoteHandoverInput(tab_);
  EXPECT_EQ(1u, leases_.HandoverInputCount(tab_));

  // After: the window is closed and its evidence has already been taken.
  EXPECT_EQ(1u, leases_.EndHandover(tab_).person_input);
  leases_.NoteHandoverInput(tab_);
  EXPECT_EQ(0u, leases_.HandoverInputCount(tab_));
}

TEST_F(ActionAuthorityTest, InputInAnotherTabIsNotThisHandoversEvidence) {
  const TabId other{"tab_other"};
  leases_.BeginHandover(tab_);

  leases_.NoteHandoverInput(other);

  EXPECT_EQ(0u, leases_.HandoverInputCount(tab_));
  EXPECT_EQ(0u, leases_.HandoverInputCount(other));
}

TEST_F(ActionAuthorityTest, TheCountSaturatesRatherThanRecordingHowMuchWasTyped) {
  leases_.BeginHandover(tab_);

  // A six-character one-time code, keystroke by keystroke. What must not
  // survive is the six.
  for (int i = 0; i < 6; ++i) {
    leases_.NoteHandoverInput(tab_);
  }
  const uint32_t six = leases_.EndHandover(tab_).person_input;

  leases_.BeginHandover(tab_);
  for (int i = 0; i < 12; ++i) {
    leases_.NoteHandoverInput(tab_);
  }
  const uint32_t twelve = leases_.EndHandover(tab_).person_input;

  EXPECT_EQ(kMaxCountedHandoverInput, six);
  EXPECT_EQ(six, twelve);
}

TEST_F(ActionAuthorityTest, AResumptionMayNotReuseTheLeaseTheHandoverRevoked) {
  const ActorLeaseId before = IssueLease(tab_, /*mutating=*/true);
  leases_.BeginHandover(tab_);

  EXPECT_TRUE(leases_.WasRevokedByHandover(tab_, before));

  // Resuming closes the window first, then mints a fresh identity. The
  // identities outlive the window in the evidence, which is what lets the
  // check survive the ordering Issue forces.
  const HandoverEvidence evidence = leases_.EndHandover(tab_);
  const ActorLeaseId after = IssueLease(tab_, /*mutating=*/true);
  EXPECT_NE(before, after);
  EXPECT_NE(evidence.revoked.end(),
            std::find(evidence.revoked.begin(), evidence.revoked.end(), before));
  EXPECT_EQ(evidence.revoked.end(),
            std::find(evidence.revoked.begin(), evidence.revoked.end(), after));
}

TEST_F(ActionAuthorityTest, AnOpenHandoverRefusesMutationAuthorityOnThatTab) {
  leases_.BeginHandover(tab_);

  // The person has this tab. A revocation the next request could undo would
  // not be a revocation, and the refusal has to live here — in the process
  // that holds the leases — rather than only in the sandboxed core's state
  // machine, which is the half an attacker would be talking to.
  ActorLeaseRequest request;
  request.task_id = TaskId{"task_1"};
  request.tab_id = tab_;
  request.mutating = true;
  request.requested_duration_ms = 1000;
  const ActorLeaseResult refused = leases_.Issue(request, Now());
  EXPECT_EQ(ActorLeaseResultCode::kAlreadyHeld, refused.code);
  EXPECT_FALSE(leases_.HasMutatingLease(tab_, Now()));

  // Another tab is unaffected: a handover is about one page, not the browser.
  const TabId other{"tab_other"};
  request.tab_id = other;
  EXPECT_EQ(ActorLeaseResultCode::kIssued, leases_.Issue(request, Now()).code);

  // And closing the window is what lets the assistant back in.
  leases_.EndHandover(tab_);
  request.tab_id = tab_;
  EXPECT_EQ(ActorLeaseResultCode::kIssued, leases_.Issue(request, Now()).code);
}

TEST_F(ActionAuthorityTest, ClosingAWindowThatWasNeverOpenInventsNoEvidence) {
  const HandoverEvidence evidence = leases_.EndHandover(tab_);
  EXPECT_EQ(0u, evidence.person_input);
  EXPECT_TRUE(evidence.revoked.empty());
}

TEST_F(ActionAuthorityTest, ASecondHandoverKeepsTheEvidenceTheFirstCollected) {
  leases_.BeginHandover(tab_);
  leases_.NoteHandoverInput(tab_);

  // A person who has typed half a code has produced exactly the evidence that
  // matters, and a repeated request must not throw it away.
  leases_.BeginHandover(tab_);

  EXPECT_EQ(1u, leases_.HandoverInputCount(tab_));
}

}  // namespace
}  // namespace taffy
