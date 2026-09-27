// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
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

class TaskActionAuthorityTest : public testing::Test {
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

  mojom::MintedCapabilityGrantPtr Grant(const ActorLeaseResult& lease) const {
    auto grant = mojom::MintedCapabilityGrant::New();
    grant->capability_id = kCapabilityId;
    grant->service_generation = kGeneration;
    grant->policy_version = 1u;
    grant->actor_lease_id = lease.lease_id.value;
    grant->task_id = kTaskId;
    grant->action_id = kActionId;
    grant->action_class = mojom::PolicyActionClass::kSyntheticClick;
    grant->operation_kind = mojom::TaskActionOperationKind::kDomClick;
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
    grant->scope->required_graph_revision = kGraphRevision;
    grant->scope->node_id = kNodeId;
    grant->scope->origin = mojom::PolicyOrigin::New();
    grant->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
    grant->scope->origin->serialization = kOrigin;
    grant->data_classes = {mojom::BipSensitivity::kNotSensitive};
    grant->effective_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
    grant->issued_at_monotonic_ms = ToMonotonicMillis(Now());
    grant->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    grant->authority_subject = mojom::AuthoritySubject::New();
    grant->authority_subject->kind = mojom::AuthoritySubjectKind::kTask;
    grant->authority_subject->authority_subject_id = kTaskId;
    return grant;
  }

  AuthorizedActionEnvelope Envelope(const ActorLeaseResult& lease) const {
    AuthorizedActionEnvelope envelope;
    envelope.dispatch_id = DispatchId{"dispatch-1"};
    envelope.action_id = ActionId{kActionId};
    envelope.task_id = TaskId{kTaskId};
    envelope.idempotency_key = kIdempotencyKey;
    envelope.action_type = ActionType::kActivate;
    envelope.target_handle.tab_id = TabId{kTabId};
    envelope.target_handle.frame_id = FrameId{kFrameId};
    envelope.target_handle.page_epoch = PageEpoch{kPageEpoch};
    envelope.target_handle.graph_revision = kGraphRevision;
    envelope.target_handle.node_id = SemanticNodeId{kNodeId};
    envelope.target_handle.expected_origin.kind = OriginKind::kTuple;
    envelope.target_handle.expected_origin.serialization = kOrigin;
    envelope.required_graph_revision = kGraphRevision;
    envelope.action_digest.algorithm = DigestAlgorithm::kSha256;
    envelope.action_digest.value = std::string(64u, 'a');
    envelope.canonical_intent_digest = crypto::SHA256Hash(CanonicalIntent());
    envelope.capability.capability_reference =
        CapabilityReference{kCapabilityId};
    envelope.capability.actor_lease_id = lease.lease_id;
    envelope.capability.policy_version = 1u;
    envelope.capability.expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    return envelope;
  }

  mojom::MintedCapabilityGrantPtr NavigateGrant(
      const ActorLeaseResult& lease) const {
    auto grant = Grant(lease);
    grant->capability_id = kNavigateCapabilityId;
    grant->action_id = kNavigateActionId;
    grant->action_class = mojom::PolicyActionClass::kOpenLink;
    grant->operation_kind = mojom::TaskActionOperationKind::kNavigate;
    grant->idempotency_key = kNavigateIdempotencyKey;
    grant->scope->node_id.reset();
    grant->scope->destination_scope = mojom::PolicyOrigin::New();
    grant->scope->destination_scope->kind = mojom::PolicyOriginKind::kTuple;
    grant->scope->destination_scope->serialization = kOrigin;
    grant->scope->destination_address = kNavigateAddress;
    return grant;
  }

  AuthorizedBrowserCommand NavigateCommand(
      const ActorLeaseResult& lease) const {
    AuthorizedBrowserCommand command;
    command.dispatch_id = DispatchId{"dispatch-navigate-1"};
    command.action_id = ActionId{kNavigateActionId};
    command.task_id = TaskId{kTaskId};
    command.idempotency_key = kNavigateIdempotencyKey;
    command.command_type = BrowserCommandType::kNavigate;
    command.tab_id = TabId{kTabId};
    command.argument = kNavigateAddress;
    command.action_digest.algorithm = DigestAlgorithm::kSha256;
    command.action_digest.value = std::string(64u, 'a');
    command.canonical_intent_digest = crypto::SHA256Hash(CanonicalIntent());
    command.capability.capability_reference =
        CapabilityReference{kNavigateCapabilityId};
    command.capability.actor_lease_id = lease.lease_id;
    command.capability.policy_version = 1u;
    command.capability.expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    return command;
  }

  mojom::MintedCapabilityGrantPtr LinkOpenGrant(
      const ActorLeaseResult& lease) const {
    auto grant = NavigateGrant(lease);
    grant->operation_kind = mojom::TaskActionOperationKind::kLinkOpen;
    grant->scope->node_id = kNodeId;
    return grant;
  }

  AuthorizedBrowserCommand LinkOpenCommand(
      const ActorLeaseResult& lease) const {
    AuthorizedBrowserCommand command = NavigateCommand(lease);
    command.command_type = BrowserCommandType::kOpenObservedLink;
    NodeHandle source;
    source.tab_id = TabId{kTabId};
    source.frame_id = FrameId{kFrameId};
    source.page_epoch = PageEpoch{kPageEpoch};
    source.graph_revision = kGraphRevision;
    source.node_id = SemanticNodeId{kNodeId};
    source.expected_origin.kind = OriginKind::kTuple;
    source.expected_origin.serialization = kOrigin;
    command.source_handle = std::move(source);
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
  static constexpr char kActionId[] = "action-click-1";
  static constexpr char kTabId[] = "tab-1";
  static constexpr char kFrameId[] = "frame-1";
  static constexpr char kPageEpoch[] = "epoch-1";
  static constexpr char kNodeId[] = "node-1";
  static constexpr char kOrigin[] = "https://example.test";
  static constexpr char kCapabilityId[] = "capability-task-click";
  static constexpr char kIdempotencyKey[] = "idempotency-task-click";
  static constexpr char kNavigateCapabilityId[] = "capability-task-navigate";
  static constexpr char kNavigateActionId[] = "action-navigate-1";
  static constexpr char kNavigateIdempotencyKey[] = "idempotency-task-navigate";
  static constexpr char kNavigateAddress[] = "https://example.test/next";
  static constexpr uint64_t kGraphRevision = 17u;

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ActorLeaseRegistry leases_;
  CapabilityLedger ledger_;
};

TEST_F(TaskActionAuthorityTest, RegisteredClickGrantIsSpentOnce) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = Grant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  CapabilityGrant copied;
  ASSERT_TRUE(ledger_.CopyRegisteredCapability(
      CapabilityReference{kCapabilityId}, copied));
  EXPECT_EQ(kCapabilityId, copied.capability_reference.value);

  const AuthorizedActionEnvelope envelope = Envelope(lease);
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskAction(envelope, leases_, TabId{kTabId}, Now()));
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.AdmitTaskAction(envelope, leases_, TabId{kTabId}, Now()));
}

TEST_F(TaskActionAuthorityTest, EveryFormOperationMatchesOnlyItsExactClass) {
  struct FormCase {
    ActionType action_type;
    mojom::PolicyActionClass action_class;
    mojom::TaskActionOperationKind operation;
  };
  constexpr std::array<FormCase, 4> kCases = {{
      {ActionType::kSetText, mojom::PolicyActionClass::kFillField,
       mojom::TaskActionOperationKind::kFormFill},
      {ActionType::kSelectOption, mojom::PolicyActionClass::kSelectOption,
       mojom::TaskActionOperationKind::kFormSelect},
      {ActionType::kToggle, mojom::PolicyActionClass::kToggleControl,
       mojom::TaskActionOperationKind::kFormToggle},
      {ActionType::kSubmitForm, mojom::PolicyActionClass::kSubmitForm,
       mojom::TaskActionOperationKind::kFormSubmit},
  }};

  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  for (size_t index = 0; index < kCases.size(); ++index) {
    const std::string suffix = std::to_string(index);
    auto grant = Grant(lease);
    grant->capability_id = "capability-form-" + suffix;
    grant->action_id = "action-form-" + suffix;
    grant->action_class = kCases[index].action_class;
    grant->operation_kind = kCases[index].operation;
    grant->effective_risk = mojom::PolicyRiskClass::kSensitiveDisclosure;
    grant->scope->node_id.reset();
    EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
              ledger_.Register(*grant, leases_, Now()));
    grant->scope->node_id = kNodeId;
    ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
              ledger_.Register(*grant, leases_, Now()));

    AuthorizedActionEnvelope envelope = Envelope(lease);
    envelope.capability.capability_reference =
        CapabilityReference{grant->capability_id};
    envelope.action_id = ActionId{grant->action_id};
    envelope.action_type = kCases[index].action_type;
    EXPECT_EQ(CapabilityAdmission::kAdmitted,
              ledger_.AdmitTaskAction(envelope, leases_, TabId{kTabId}, Now()));
  }
}

TEST_F(TaskActionAuthorityTest, BoundIdentityOrNodeMismatchDoesNotSpend) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = Grant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  AuthorizedActionEnvelope wrong_digest = Envelope(lease);
  wrong_digest.action_digest.value = std::string(64u, 'b');
  EXPECT_EQ(
      CapabilityAdmission::kDigestMismatch,
      ledger_.AdmitTaskAction(wrong_digest, leases_, TabId{kTabId}, Now()));

  AuthorizedActionEnvelope wrong_node = Envelope(lease);
  wrong_node.target_handle.node_id = SemanticNodeId{"node-other"};
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskAction(wrong_node, leases_, TabId{kTabId}, Now()));

  AuthorizedActionEnvelope wrong_intent = Envelope(lease);
  wrong_intent.canonical_intent_digest[0] ^= 0xffu;
  EXPECT_EQ(
      CapabilityAdmission::kDigestMismatch,
      ledger_.AdmitTaskAction(wrong_intent, leases_, TabId{kTabId}, Now()));

  AuthorizedActionEnvelope wrong_idempotency = Envelope(lease);
  wrong_idempotency.idempotency_key = "idempotency-task-other";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskAction(wrong_idempotency, leases_,
                                    TabId{kTabId}, Now()));

  EXPECT_EQ(
      CapabilityAdmission::kAdmitted,
      ledger_.AdmitTaskAction(Envelope(lease), leases_, TabId{kTabId}, Now()));
}

TEST_F(TaskActionAuthorityTest, RegisteredNavigateGrantIsSpentOnce) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = NavigateGrant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  const AuthorizedBrowserCommand command = NavigateCommand(lease);
  EXPECT_EQ(
      CapabilityAdmission::kAdmitted,
      ledger_.AdmitTaskBrowserCommand(command, leases_, TabId{kTabId}, Now()));
  EXPECT_EQ(
      CapabilityAdmission::kAlreadySpent,
      ledger_.AdmitTaskBrowserCommand(command, leases_, TabId{kTabId}, Now()));
}

// The address a model types is an origin and nothing else - "go to
// example.test", not a path on it. Every navigate vector above this line
// carries a path, which is why the ledger refused a bare origin for a missing
// trailing slash and nothing in this suite noticed. The refusal discarded a
// grant policy had already minted, and reached the model as
// `Deny(Unsupported)` on the first move of every errand.
TEST_F(TaskActionAuthorityTest, ABareOriginDestinationRegisters) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  auto grant = NavigateGrant(lease);
  grant->scope->destination_address = kOrigin;
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));
}

// And nothing else is admitted: a spelling that is not what it resolves to is
// still refused, which is what the clause exists for.
TEST_F(TaskActionAuthorityTest, ANonCanonicalDestinationIsStillRefused) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  auto grant = NavigateGrant(lease);
  grant->scope->destination_address = "https://example.test/a/../next";
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
            ledger_.Register(*grant, leases_, Now()));

  auto second = NavigateGrant(lease);
  second->capability_id = "capability-task-navigate-2";
  second->scope->destination_address = "https://example.test:443/next";
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kInvalidGrant,
            ledger_.Register(*second, leases_, Now()));
}

TEST_F(TaskActionAuthorityTest, NavigateDigestMismatchDoesNotSpend) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = NavigateGrant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  AuthorizedBrowserCommand wrong_digest = NavigateCommand(lease);
  wrong_digest.action_digest.value = std::string(64u, 'b');
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_digest, leases_,
                                            TabId{kTabId}, Now()));

  AuthorizedBrowserCommand wrong_destination = NavigateCommand(lease);
  wrong_destination.argument = "https://example.test/other";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_destination, leases_,
                                            TabId{kTabId}, Now()));

  AuthorizedBrowserCommand wrong_operation = NavigateCommand(lease);
  wrong_operation.command_type = BrowserCommandType::kGoBack;
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_operation, leases_,
                                            TabId{kTabId}, Now()));

  AuthorizedBrowserCommand unexpected_source = NavigateCommand(lease);
  unexpected_source.source_handle = LinkOpenCommand(lease).source_handle;
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(unexpected_source, leases_,
                                            TabId{kTabId}, Now()));

  AuthorizedBrowserCommand wrong_intent = NavigateCommand(lease);
  wrong_intent.canonical_intent_digest[0] ^= 0xffu;
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_intent, leases_,
                                            TabId{kTabId}, Now()));

  AuthorizedBrowserCommand wrong_idempotency = NavigateCommand(lease);
  wrong_idempotency.idempotency_key = "idempotency-task-navigate-other";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_idempotency, leases_,
                                            TabId{kTabId}, Now()));

  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskBrowserCommand(NavigateCommand(lease), leases_,
                                            TabId{kTabId}, Now()));
}

TEST_F(TaskActionAuthorityTest, ObservedLinkBindsItsExactLiveSource) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = LinkOpenGrant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  AuthorizedBrowserCommand missing_source = LinkOpenCommand(lease);
  missing_source.source_handle.reset();
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(missing_source, leases_,
                                            TabId{kTabId}, Now()));

  AuthorizedBrowserCommand wrong_node = LinkOpenCommand(lease);
  wrong_node.source_handle->node_id = SemanticNodeId{"node-other"};
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_node, leases_, TabId{kTabId},
                                            Now()));

  AuthorizedBrowserCommand wrong_revision = LinkOpenCommand(lease);
  wrong_revision.source_handle->graph_revision += 1u;
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_revision, leases_,
                                            TabId{kTabId}, Now()));

  AuthorizedBrowserCommand wrong_origin = LinkOpenCommand(lease);
  wrong_origin.source_handle->expected_origin.serialization =
      "https://different.test";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_origin, leases_,
                                            TabId{kTabId}, Now()));

  AuthorizedBrowserCommand wrong_destination = LinkOpenCommand(lease);
  wrong_destination.argument = "https://example.test/other";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskBrowserCommand(wrong_destination, leases_,
                                            TabId{kTabId}, Now()));

  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskBrowserCommand(LinkOpenCommand(lease), leases_,
                                            TabId{kTabId}, Now()));
}

TEST_F(TaskActionAuthorityTest, ANodeGrantCannotAdmitABrowserCommand) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  const auto grant = Grant(lease);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  AuthorizedBrowserCommand command = NavigateCommand(lease);
  command.action_id = ActionId{kActionId};
  command.capability.capability_reference = CapabilityReference{kCapabilityId};
  EXPECT_EQ(
      CapabilityAdmission::kDigestMismatch,
      ledger_.AdmitTaskBrowserCommand(command, leases_, TabId{kTabId}, Now()));

  EXPECT_EQ(
      CapabilityAdmission::kAdmitted,
      ledger_.AdmitTaskAction(Envelope(lease), leases_, TabId{kTabId}, Now()));
}

}  // namespace
}  // namespace taffy
