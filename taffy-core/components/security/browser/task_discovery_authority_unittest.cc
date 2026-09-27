// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "base/test/task_environment.h"
#include "content/public/test/browser_task_environment.h"
#include "crypto/sha2.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/components/security/browser/action_authority.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

class TaskDiscoveryAuthorityTest : public testing::Test {
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
    request.mutating = true;
    return leases_.Issue(request, Now());
  }

  mojom::MintedCapabilityGrantPtr DiscoveryGrant(
      const ActorLeaseResult& lease) const {
    auto grant = mojom::MintedCapabilityGrant::New();
    grant->capability_id = kCapabilityId;
    grant->service_generation = kGeneration;
    grant->policy_version = 1u;
    grant->actor_lease_id = lease.lease_id.value;
    grant->task_id = kTaskId;
    grant->action_id = kActionId;
    grant->action_class = mojom::PolicyActionClass::kOpenLink;
    grant->operation_kind = mojom::TaskActionOperationKind::kSearch;
    const auto canonical_intent_digest = crypto::SHA256Hash(CanonicalIntent());
    grant->canonical_intent_digest.assign(canonical_intent_digest.begin(),
                                          canonical_intent_digest.end());
    grant->principal = mojom::PolicyPrincipal::New();
    grant->principal->kind = mojom::PolicyPrincipalKind::kAssistant;
    grant->proposal_digest = std::string(64u, 'a');
    grant->idempotency_key = kIdempotencyKey;
    grant->scope = mojom::PolicyCapabilityScope::New();
    grant->scope->profile_id = kProfileId;
    grant->scope->tab_id = kTabId;
    grant->scope->frame_id = kFrameId;
    grant->scope->page_epoch = kPageEpoch;
    grant->scope->required_graph_revision = 0u;
    grant->scope->origin = mojom::PolicyOrigin::New();
    grant->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
    grant->scope->origin->opaque_id = kOpaqueOriginId;
    grant->scope->destination_scope = mojom::PolicyOrigin::New();
    grant->scope->destination_scope->kind = mojom::PolicyOriginKind::kTuple;
    grant->scope->destination_scope->serialization = kSearchOrigin;
    grant->scope->destination_address = kSearchAddress;
    grant->data_classes = {mojom::BipSensitivity::kNotSensitive};
    grant->effective_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
    grant->issued_at_monotonic_ms = ToMonotonicMillis(Now());
    grant->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    grant->authority_subject = mojom::AuthoritySubject::New();
    grant->authority_subject->kind = mojom::AuthoritySubjectKind::kTask;
    grant->authority_subject->authority_subject_id = kTaskId;
    grant->discovery =
        mojom::TaskDiscoveryAuthorityFact::New(kTabId, kBrowserSessionId, 3u);
    return grant;
  }

  AuthorizedBrowserCommand DiscoveryCommand(
      const ActorLeaseResult& lease) const {
    AuthorizedBrowserCommand command;
    command.dispatch_id = DispatchId{"dispatch-discovery-1"};
    command.action_id = ActionId{kActionId};
    command.task_id = TaskId{kTaskId};
    command.idempotency_key = kIdempotencyKey;
    command.command_type = BrowserCommandType::kSearch;
    command.tab_id = TabId{kTabId};
    command.argument = kSearchAddress;
    command.transient_search_query = "tea";
    command.action_digest.algorithm = DigestAlgorithm::kSha256;
    command.action_digest.value = std::string(64u, 'a');
    command.canonical_intent_digest = crypto::SHA256Hash(CanonicalIntent());
    command.capability.capability_reference =
        CapabilityReference{kCapabilityId};
    command.capability.actor_lease_id = lease.lease_id;
    command.capability.policy_version = 1u;
    command.capability.expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    command.capability.task_discovery = TaskDiscoveryCapabilityBinding{
        .tab_id = TabId{kTabId},
        .frame_id = FrameId{kFrameId},
        .page_epoch = PageEpoch{kPageEpoch},
        .opaque_origin_id = kOpaqueOriginId,
        .browser_session_id = kBrowserSessionId,
        .remaining_new_source_cap = 3u,
    };
    return command;
  }

  mojom::MintedCapabilityGrantPtr NavigationGrant(
      const ActorLeaseResult& lease) const {
    auto grant = DiscoveryGrant(lease);
    grant->operation_kind = mojom::TaskActionOperationKind::kNavigate;
    grant->scope->destination_scope->serialization = "https://official.test";
    grant->scope->destination_address = "https://official.test/start";
    return grant;
  }

  AuthorizedBrowserCommand NavigationCommand(
      const ActorLeaseResult& lease) const {
    auto command = DiscoveryCommand(lease);
    command.command_type = BrowserCommandType::kNavigate;
    command.argument = "https://official.test/start";
    command.transient_search_query.reset();
    return command;
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
  static constexpr char kActionId[] = "action-discovery-1";
  static constexpr char kTabId[] = "tab-1";
  static constexpr char kFrameId[] = "frame-1";
  static constexpr char kPageEpoch[] = "epoch-1";
  static constexpr char kCapabilityId[] = "capability-task-discovery";
  static constexpr char kIdempotencyKey[] = "idempotency-task-discovery";
  static constexpr char kOpaqueOriginId[] = "opaque-blank-1";
  static constexpr char kBrowserSessionId[] = "browser-session-1";
  static constexpr char kSearchOrigin[] = "https://search.test";
  static constexpr char kSearchAddress[] = "https://search.test/?q=tea";

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ActorLeaseRegistry leases_;
  CapabilityLedger ledger_;
};

TEST_F(TaskDiscoveryAuthorityTest,
       GrantRequiresExactOpaqueScopeAndDispatchFact) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = DiscoveryGrant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  CapabilityGrant copied;
  ASSERT_TRUE(ledger_.CopyRegisteredCapability(
      CapabilityReference{kCapabilityId}, copied));
  ASSERT_TRUE(copied.task_discovery);
  EXPECT_EQ(copied.task_discovery->opaque_origin_id, kOpaqueOriginId);
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskBrowserCommand(DiscoveryCommand(lease), leases_,
                                            TabId{kTabId}, Now()));
}

TEST_F(TaskDiscoveryAuthorityTest,
       KnownAddressUsesExactDiscoveryGrantOnlyOnce) {
  const auto lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*NavigationGrant(lease), leases_, Now()));
  CapabilityGrant copied;
  ASSERT_TRUE(ledger_.CopyRegisteredCapability(
      CapabilityReference{kCapabilityId}, copied));
  ASSERT_TRUE(copied.task_discovery);
  EXPECT_EQ(copied.task_discovery->opaque_origin_id, kOpaqueOriginId);
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskBrowserCommand(NavigationCommand(lease), leases_,
                                            TabId{kTabId}, Now()));
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.AdmitTaskBrowserCommand(NavigationCommand(lease), leases_,
                                            TabId{kTabId}, Now()));
}

TEST_F(TaskDiscoveryAuthorityTest,
       KnownAddressRejectsWiderOrInsecureDiscoveryGrants) {
  const auto lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  for (int mutation = 0; mutation < 8; ++mutation) {
    SCOPED_TRACE(mutation);
    auto grant = NavigationGrant(lease);
    switch (mutation) {
      case 0:
        grant->scope->destination_scope->serialization = "http://official.test";
        grant->scope->destination_address = "http://official.test/start";
        break;
      case 1:
        grant->scope->destination_address = "https://person:key@official.test/";
        break;
      case 2:
        grant->scope->destination_scope->serialization = "https://other.test";
        break;
      case 3:
        grant->discovery->remaining_new_source_cap = 0u;
        break;
      case 4:
        grant->discovery->remaining_new_source_cap =
            mojom::kMaxNewSourceCap + 1u;
        break;
      case 5:
        grant->discovery->discovery_tab_id = "other-tab";
        break;
      case 6:
        grant->scope->required_graph_revision = 1u;
        break;
      case 7:
        grant->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
        grant->scope->origin->opaque_id.reset();
        grant->scope->origin->serialization = "https://source.test";
        break;
    }
    EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
              ledger_.Register(*grant, leases_, Now()));
  }
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*NavigationGrant(lease), leases_, Now()));
}

TEST_F(TaskDiscoveryAuthorityTest,
       KnownAddressDispatchSubstitutionsDoNotSpendItsCapability) {
  const auto lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*NavigationGrant(lease), leases_, Now()));
  for (int mutation = 0; mutation < 12; ++mutation) {
    SCOPED_TRACE(mutation);
    auto command = NavigationCommand(lease);
    auto& discovery = *command.capability.task_discovery;
    switch (mutation) {
      case 0:
        discovery.browser_session_id = "other-session";
        break;
      case 1:
        discovery.tab_id = TabId{"other-tab"};
        break;
      case 2:
        discovery.frame_id = FrameId{"other-frame"};
        break;
      case 3:
        discovery.page_epoch = PageEpoch{"other-epoch"};
        break;
      case 4:
        discovery.opaque_origin_id = "other-origin";
        break;
      case 5:
        discovery.remaining_new_source_cap = 2u;
        break;
      case 6:
        command.action_digest.value = std::string(64u, 'b');
        break;
      case 7:
        command.canonical_intent_digest[0] ^= 1u;
        break;
      case 8:
        command.capability.actor_lease_id = ActorLeaseId{"other-lease"};
        break;
      case 9:
        command.argument = "https://official.test/other";
        break;
      case 10:
        command.command_type = BrowserCommandType::kSearch;
        command.transient_search_query = "substituted search";
        break;
      case 11:
        command.transient_search_query = "unexpected search";
        break;
    }
    EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
              ledger_.AdmitTaskBrowserCommand(command, leases_, TabId{kTabId},
                                              Now()));
  }
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskBrowserCommand(NavigationCommand(lease), leases_,
                                            TabId{kTabId}, Now()));
}

TEST_F(TaskDiscoveryAuthorityTest, KnownAddressCannotOutliveItsActorLease) {
  const auto lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*NavigationGrant(lease), leases_, Now()));
  leases_.Release(lease.lease_id);
  EXPECT_EQ(CapabilityAdmission::kLeaseNotForThisTab,
            ledger_.AdmitTaskBrowserCommand(NavigationCommand(lease), leases_,
                                            TabId{kTabId}, Now()));
}

// Decision 0176 section 4. An opaque scope used to mean one thing — a browser
// owned discovery blank — so a grant carrying one without a discovery fact was
// refused outright. It now means two, because the error document Chromium
// writes when an address does not answer is also a document with no site of
// its own, and a task standing on one must be able to leave it. So the rule is
// no longer "opaque implies discovery"; it is that an opaque scope is admitted
// for a discovery shape or for a departure, and for nothing else.
//
// The three cases below are that sentence: the departure that is now admitted,
// the same opaque scope on a move that does not leave the document, and a
// discovery fact claimed on a document that does have a site. Each grant needs
// its own capability id, because the ledger is keyed by it.
TEST_F(TaskDiscoveryAuthorityTest,
       OnlyADepartureOrADiscoveryShapeMayCarryAnOpaqueScope) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);

  // A search leaving a document with no site: `kOpenLink`, a tuple
  // destination, no node and no revision floor.
  auto departure = DiscoveryGrant(lease);
  departure->discovery.reset();
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*departure, leases_, Now()));

  // The same opaque scope on a move that does not leave the document. Opening
  // a tab is a valid class and operation pair, so the only clause that can
  // refuse it is the departure shape itself.
  auto opaque_tab_open = DiscoveryGrant(lease);
  opaque_tab_open->capability_id = "capability-task-opaque-tab-open";
  opaque_tab_open->discovery.reset();
  opaque_tab_open->action_class = mojom::PolicyActionClass::kCreateTaskTab;
  opaque_tab_open->operation_kind = mojom::TaskActionOperationKind::kTabsOpen;
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
            ledger_.Register(*opaque_tab_open, leases_, Now()));

  auto tuple_discovery = DiscoveryGrant(lease);
  tuple_discovery->capability_id = "capability-task-tuple-discovery";
  tuple_discovery->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
  tuple_discovery->scope->origin->opaque_id.reset();
  tuple_discovery->scope->origin->serialization = "https://source.test";
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
            ledger_.Register(*tuple_discovery, leases_, Now()));
}

TEST_F(TaskDiscoveryAuthorityTest,
       DispatchMismatchDoesNotSpendExactCapability) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = DiscoveryGrant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  AuthorizedBrowserCommand wrong_session = DiscoveryCommand(lease);
  wrong_session.capability.task_discovery->browser_session_id = "stale-session";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_session, leases_,
                                            TabId{kTabId}, Now()));
  AuthorizedBrowserCommand missing_discovery = DiscoveryCommand(lease);
  missing_discovery.capability.task_discovery.reset();
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(missing_discovery, leases_,
                                            TabId{kTabId}, Now()));
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskBrowserCommand(DiscoveryCommand(lease), leases_,
                                            TabId{kTabId}, Now()));
}

}  // namespace
}  // namespace taffy
