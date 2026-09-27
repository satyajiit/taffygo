// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <optional>
#include <string>
#include <string_view>
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

class TaskActionAuthorityControlTest : public testing::Test {
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

  mojom::MintedCapabilityGrantPtr ControlGrant(
      const ActorLeaseResult& lease,
      mojom::TaskActionOperationKind operation,
      std::string_view suffix) const {
    auto grant = mojom::MintedCapabilityGrant::New();
    grant->capability_id = "capability-task-control-" + std::string(suffix);
    grant->service_generation = kGeneration;
    grant->policy_version = 1u;
    grant->actor_lease_id = lease.lease_id.value;
    grant->task_id = kTaskId;
    grant->action_id = "action-control-" + std::string(suffix);
    grant->action_class = mojom::PolicyActionClass::kControlTab;
    grant->operation_kind = operation;
    const auto digest = crypto::SHA256Hash(CanonicalIntent());
    grant->canonical_intent_digest.assign(digest.begin(), digest.end());
    grant->principal = mojom::PolicyPrincipal::New();
    grant->principal->kind = mojom::PolicyPrincipalKind::kAssistant;
    grant->proposal_digest = std::string(64u, 'a');
    grant->idempotency_key = "idempotency-task-control-" + std::string(suffix);
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
    grant->effective_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
    grant->issued_at_monotonic_ms = ToMonotonicMillis(Now());
    grant->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    grant->authority_subject = mojom::AuthoritySubject::New();
    grant->authority_subject->kind = mojom::AuthoritySubjectKind::kTask;
    grant->authority_subject->authority_subject_id = kTaskId;
    return grant;
  }

  AuthorizedBrowserCommand ControlCommand(const ActorLeaseResult& lease,
                                          BrowserCommandType command_type,
                                          std::string_view suffix) const {
    AuthorizedBrowserCommand command;
    command.dispatch_id = DispatchId{"dispatch-control-" + std::string(suffix)};
    command.action_id = ActionId{"action-control-" + std::string(suffix)};
    command.task_id = TaskId{kTaskId};
    command.idempotency_key =
        "idempotency-task-control-" + std::string(suffix);
    command.command_type = command_type;
    command.tab_id = TabId{kTabId};
    command.action_digest.algorithm = DigestAlgorithm::kSha256;
    command.action_digest.value = std::string(64u, 'a');
    command.canonical_intent_digest = crypto::SHA256Hash(CanonicalIntent());
    command.capability.capability_reference =
        CapabilityReference{"capability-task-control-" + std::string(suffix)};
    command.capability.actor_lease_id = lease.lease_id;
    command.capability.policy_version = 1u;
    command.capability.expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    return command;
  }

  mojom::MintedCapabilityGrantPtr TabListGrant(
      const ActorLeaseResult& lease) const {
    auto grant = ControlGrant(
        lease, mojom::TaskActionOperationKind::kTabsList, "list");
    grant->action_class = mojom::PolicyActionClass::kObservePage;
    grant->idempotency_key = kTabListIdempotencyKey;
    return grant;
  }

  mojom::TaskActionEffectPtr TabListAction() const {
    auto action = mojom::TaskActionEffect::New();
    action->action_id = "action-control-list";
    action->proposal_digest = std::string(64u, 'a');
    action->idempotency_key = kTabListIdempotencyKey;
    action->capability_id = "capability-task-control-list";
    action->document = mojom::TaskFrozenDocument::New(
        kFrameId, kPageEpoch, kGraphRevision, kOrigin, std::nullopt);
    action->executable = mojom::TaskExecutableAction::New();
    action->executable->action_class = mojom::PolicyActionClass::kObservePage;
    action->executable->operation_kind =
        mojom::TaskActionOperationKind::kTabsList;
    action->executable->canonical_intent = CanonicalIntent();
    action->executable->tab_id = kTabId;
    action->executable->task_tab = mojom::TaskTabActionBinding::New();
    action->executable->task_tab->browser_session_id = "browser-session-1";
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
  static constexpr char kTabId[] = "tab-1";
  static constexpr char kFrameId[] = "frame-1";
  static constexpr char kPageEpoch[] = "epoch-1";
  static constexpr char kOrigin[] = "https://example.test";
  static constexpr char kNavigateAddress[] = "https://example.test/next";
  static constexpr char kTabListIdempotencyKey[] =
      "idempotency-task-tab-list";
  static constexpr uint64_t kGraphRevision = 17u;

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ActorLeaseRegistry leases_;
  CapabilityLedger ledger_;
};

TEST_F(TaskActionAuthorityControlTest,
       TabControlsRequireAnExactDestinationFreeGrant) {
  struct ControlCase {
    BrowserCommandType command_type;
    mojom::TaskActionOperationKind operation;
    std::string_view suffix;
  };
  constexpr std::array<ControlCase, 4> kCases = {{
      {BrowserCommandType::kGoBack,
       mojom::TaskActionOperationKind::kHistoryBack, "back"},
      {BrowserCommandType::kGoForward,
       mojom::TaskActionOperationKind::kHistoryForward, "forward"},
      {BrowserCommandType::kReload, mojom::TaskActionOperationKind::kReload,
       "reload"},
      {BrowserCommandType::kStopLoading,
       mojom::TaskActionOperationKind::kStopLoading, "stop"},
  }};

  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  for (const ControlCase& control : kCases) {
    auto grant = ControlGrant(lease, control.operation, control.suffix);
    ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
              ledger_.Register(*grant, leases_, Now()));

    AuthorizedBrowserCommand invented_destination =
        ControlCommand(lease, control.command_type, control.suffix);
    invented_destination.argument = kNavigateAddress;
    EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
              ledger_.AdmitTaskBrowserCommand(invented_destination, leases_,
                                              TabId{kTabId}, Now()));

    AuthorizedBrowserCommand wrong_operation =
        ControlCommand(lease, BrowserCommandType::kNavigate, control.suffix);
    EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
              ledger_.AdmitTaskBrowserCommand(wrong_operation, leases_,
                                              TabId{kTabId}, Now()));

    EXPECT_EQ(CapabilityAdmission::kAdmitted,
              ledger_.AdmitTaskBrowserCommand(
                  ControlCommand(lease, control.command_type, control.suffix),
                  leases_, TabId{kTabId}, Now()));
  }
}

TEST_F(TaskActionAuthorityControlTest,
       TaskTabEffectCannotSubstituteItsIdempotencyKey) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*TabListGrant(lease), leases_, Now()));

  mojom::TaskActionEffectPtr substituted = TabListAction();
  substituted->idempotency_key = "idempotency-task-tab-other";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskTabAction(*substituted, kTaskId, leases_, Now()));

  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskTabAction(*TabListAction(), kTaskId, leases_,
                                       Now()));
}

}  // namespace
}  // namespace taffy
