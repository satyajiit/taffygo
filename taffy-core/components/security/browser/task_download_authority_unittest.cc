// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <string_view>
#include <vector>

#include "base/test/task_environment.h"
#include "content/public/test/browser_task_environment.h"
#include "crypto/sha2.h"
#include "taffy/components/security/browser/action_authority.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

class TaskDownloadAuthorityTest : public testing::Test {
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

  static std::vector<uint8_t> CanonicalIntent(bool starts) {
    return starts ? std::vector<uint8_t>{0x0fu, 0xa1u, 0x01u}
                  : std::vector<uint8_t>{0x10u, 0xa1u, 0x02u};
  }

  static std::vector<uint8_t> CanonicalCancelIntent(
      std::string_view download_id) {
    std::vector<uint8_t> intent{0x1bu, 0xa1u, 0x03u};
    intent.insert(intent.end(), download_id.begin(), download_id.end());
    return intent;
  }

  mojom::MintedCapabilityGrantPtr Grant(const ActorLeaseResult& lease,
                                        bool starts) const {
    auto grant = mojom::MintedCapabilityGrant::New();
    grant->capability_id = starts ? kStartCapabilityId : kListCapabilityId;
    grant->service_generation = kGeneration;
    grant->policy_version = 1u;
    grant->actor_lease_id = lease.lease_id.value;
    grant->task_id = kTaskId;
    grant->action_id = starts ? kStartActionId : kListActionId;
    grant->action_class = starts ? mojom::PolicyActionClass::kStartDownload
                                 : mojom::PolicyActionClass::kObservePage;
    grant->operation_kind = starts
                                ? mojom::TaskActionOperationKind::kDownloadStart
                                : mojom::TaskActionOperationKind::kDownloadList;
    const auto canonical_digest = crypto::SHA256Hash(CanonicalIntent(starts));
    grant->canonical_intent_digest.assign(canonical_digest.begin(),
                                          canonical_digest.end());
    grant->principal = mojom::PolicyPrincipal::New();
    grant->principal->kind = mojom::PolicyPrincipalKind::kAssistant;
    grant->proposal_digest = std::string(64u, 'a');
    grant->idempotency_key =
        starts ? "download-start-key" : "download-list-key";
    grant->scope = mojom::PolicyCapabilityScope::New();
    grant->scope->profile_id = kProfileId;
    grant->scope->tab_id = kTabId;
    grant->scope->frame_id = kFrameId;
    grant->scope->page_epoch = kPageEpoch;
    grant->scope->required_graph_revision = kGraphRevision;
    grant->scope->origin = mojom::PolicyOrigin::New();
    grant->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
    grant->scope->origin->serialization = kOrigin;
    if (starts) {
      grant->scope->destination_scope = mojom::PolicyOrigin::New();
      grant->scope->destination_scope->kind = mojom::PolicyOriginKind::kTuple;
      grant->scope->destination_scope->serialization = kDestinationOrigin;
      grant->scope->destination_address = kDestinationAddress;
    }
    grant->data_classes = {mojom::BipSensitivity::kNotSensitive};
    grant->effective_risk = starts
                                ? mojom::PolicyRiskClass::kReversibleDisclosure
                                : mojom::PolicyRiskClass::kLocalRead;
    grant->issued_at_monotonic_ms = ToMonotonicMillis(Now());
    grant->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    grant->authority_subject = mojom::AuthoritySubject::New();
    grant->authority_subject->kind = mojom::AuthoritySubjectKind::kTask;
    grant->authority_subject->authority_subject_id = kTaskId;
    return grant;
  }

  mojom::TaskActionEffectPtr Action(bool starts) const {
    auto action = mojom::TaskActionEffect::New();
    action->action_id = starts ? kStartActionId : kListActionId;
    action->proposal_digest = std::string(64u, 'a');
    action->idempotency_key =
        starts ? "download-start-key" : "download-list-key";
    action->capability_id = starts ? kStartCapabilityId : kListCapabilityId;
    action->document = mojom::TaskFrozenDocument::New(
        kFrameId, kPageEpoch, kGraphRevision, kOrigin, std::nullopt);
    action->executable = mojom::TaskExecutableAction::New();
    action->executable->action_class =
        starts ? mojom::PolicyActionClass::kStartDownload
               : mojom::PolicyActionClass::kObservePage;
    action->executable->operation_kind =
        starts ? mojom::TaskActionOperationKind::kDownloadStart
               : mojom::TaskActionOperationKind::kDownloadList;
    action->executable->canonical_intent = CanonicalIntent(starts);
    action->executable->tab_id = kTabId;
    action->executable->task_download = mojom::TaskDownloadActionBinding::New();
    action->executable->task_download->browser_session_id = kBrowserSessionId;
    if (starts) {
      action->executable->destination_origin = kDestinationOrigin;
      action->executable->destination_address = kDestinationAddress;
    }
    return action;
  }

  mojom::MintedCapabilityGrantPtr CancelGrant(
      const ActorLeaseResult& lease) const {
    mojom::MintedCapabilityGrantPtr grant = Grant(lease, false);
    grant->capability_id = kCancelCapabilityId;
    grant->action_id = kCancelActionId;
    grant->action_class = mojom::PolicyActionClass::kStartDownload;
    grant->operation_kind = mojom::TaskActionOperationKind::kDownloadCancel;
    const auto canonical_digest =
        crypto::SHA256Hash(CanonicalCancelIntent(kDownloadId));
    grant->canonical_intent_digest.assign(canonical_digest.begin(),
                                          canonical_digest.end());
    grant->idempotency_key = "download-cancel-key";
    grant->effective_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
    return grant;
  }

  mojom::TaskActionEffectPtr CancelAction(std::string_view download_id) const {
    mojom::TaskActionEffectPtr action = Action(false);
    action->action_id = kCancelActionId;
    action->idempotency_key = "download-cancel-key";
    action->capability_id = kCancelCapabilityId;
    action->executable->action_class = mojom::PolicyActionClass::kStartDownload;
    action->executable->operation_kind =
        mojom::TaskActionOperationKind::kDownloadCancel;
    action->executable->canonical_intent = CanonicalCancelIntent(download_id);
    action->executable->task_download->download_id = std::string(download_id);
    return action;
  }

  static uint64_t ToMonotonicMillis(base::TimeTicks value) {
    const int64_t milliseconds = (value - base::TimeTicks()).InMilliseconds();
    return milliseconds < 0 ? 0u : static_cast<uint64_t>(milliseconds);
  }

  static constexpr uint64_t kGeneration = 9u;
  static constexpr uint64_t kGraphRevision = 17u;
  static constexpr char kProfileId[] = "profile-download";
  static constexpr char kTaskId[] = "task-download";
  static constexpr char kTabId[] = "tab-1";
  static constexpr char kFrameId[] = "frame-1";
  static constexpr char kPageEpoch[] = "epoch-1";
  static constexpr char kOrigin[] = "https://example.test";
  static constexpr char kDestinationOrigin[] = "https://files.example.test";
  static constexpr char kDestinationAddress[] =
      "https://files.example.test/report.pdf";
  static constexpr char kBrowserSessionId[] = "browser-session-1";
  static constexpr char kStartCapabilityId[] = "capability-download-start";
  static constexpr char kListCapabilityId[] = "capability-download-list";
  static constexpr char kCancelCapabilityId[] = "capability-download-cancel";
  static constexpr char kStartActionId[] = "action-download-start";
  static constexpr char kListActionId[] = "action-download-list";
  static constexpr char kCancelActionId[] = "action-download-cancel";
  static constexpr char kDownloadId[] = "opaque-guid-1";

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ActorLeaseRegistry leases_;
  CapabilityLedger ledger_;
};

TEST_F(TaskDownloadAuthorityTest,
       StartGrantBindsExactDestinationAndSpendsOnce) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*Grant(lease, true), leases_, Now()));

  mojom::TaskActionEffectPtr wrong_destination = Action(true);
  wrong_destination->executable->destination_address =
      "https://files.example.test/other.pdf";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskDownloadAction(*wrong_destination, kTaskId,
                                            leases_, Now()));

  mojom::TaskActionEffectPtr wrong_idempotency = Action(true);
  wrong_idempotency->idempotency_key = "download-other-key";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskDownloadAction(*wrong_idempotency, kTaskId,
                                            leases_, Now()));

  mojom::TaskActionEffectPtr exact = Action(true);
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskDownloadAction(*exact, kTaskId, leases_, Now()));
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.AdmitTaskDownloadAction(*exact, kTaskId, leases_, Now()));
}

TEST_F(TaskDownloadAuthorityTest, LinkGrantBindsOneExactControlAndSpendsOnce) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  auto grant = Grant(lease, true);
  grant->scope->node_id = "node-download";
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*grant, leases_, Now()));

  auto action = Action(true);
  action->executable->node_id = "node-other";
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskDownloadAction(*action, kTaskId, leases_, Now()));
  action->executable->node_id.reset();
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskDownloadAction(*action, kTaskId, leases_, Now()));
  action->executable->node_id = "node-download";
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskDownloadAction(*action, kTaskId, leases_, Now()));
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.AdmitTaskDownloadAction(*action, kTaskId, leases_, Now()));
}

TEST_F(TaskDownloadAuthorityTest,
       ListGrantRejectsDestinationAndCanonicalWidening) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*Grant(lease, false), leases_, Now()));

  mojom::TaskActionEffectPtr action = Action(false);
  action->executable->destination_origin = kDestinationOrigin;
  action->executable->destination_address = kDestinationAddress;
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskDownloadAction(*action, kTaskId, leases_, Now()));
  action = Action(false);
  action->executable->canonical_intent.push_back(0xffu);
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskDownloadAction(*action, kTaskId, leases_, Now()));

  action = Action(false);
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskDownloadAction(*action, kTaskId, leases_, Now()));
}

TEST_F(TaskDownloadAuthorityTest,
       CancelGrantBindsExactOpaqueIdentityAndSpendsOnce) {
  const ActorLeaseResult lease = IssueLease();
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_EQ(mojom::CapabilityRegistrationStatus::kRegistered,
            ledger_.Register(*CancelGrant(lease), leases_, Now()));

  mojom::TaskActionEffectPtr wrong_download = CancelAction("opaque-guid-other");
  EXPECT_EQ(CapabilityAdmission::kDigestMismatch,
            ledger_.AdmitTaskDownloadAction(*wrong_download, kTaskId, leases_,
                                            Now()));

  mojom::TaskActionEffectPtr exact = CancelAction(kDownloadId);
  EXPECT_EQ(CapabilityAdmission::kAdmitted,
            ledger_.AdmitTaskDownloadAction(*exact, kTaskId, leases_, Now()));
  EXPECT_EQ(CapabilityAdmission::kAlreadySpent,
            ledger_.AdmitTaskDownloadAction(*exact, kTaskId, leases_, Now()));
}

}  // namespace
}  // namespace taffy
