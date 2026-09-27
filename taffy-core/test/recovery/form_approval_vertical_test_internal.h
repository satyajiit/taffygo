// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_FORM_APPROVAL_VERTICAL_TEST_INTERNAL_H_
#define TAFFY_TEST_RECOVERY_FORM_APPROVAL_VERTICAL_TEST_INTERNAL_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/core_service_manager_types.h"
#include "taffy/browser/field_values/field_value_surface.mojom.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/test/recovery/form_approval_vertical_test_support.h"

namespace taffy::test {

namespace service = core_service::mojom;
namespace surface = browser::field_values::mojom;

uint64_t FormTestMonotonicMillis();
uint64_t FormTestUtcMillis();

class FormVerticalObserver final : public CoreServiceObserver {};

class RecordingFieldValueClient final : public surface::TaffyFieldValueClient {
 public:
  RecordingFieldValueClient();
  ~RecordingFieldValueClient() override;

  mojo::PendingRemote<surface::TaffyFieldValueClient> Bind();
  void Open(surface::FieldValueRequestPtr request) override;
  void Close(const std::string& request_id,
             surface::FieldValueCloseReason reason) override;

  const surface::FieldValueRequest* request() const { return request_.get(); }

 private:
  surface::FieldValueRequestPtr request_;
  mojo::Receiver<surface::TaffyFieldValueClient> receiver_{this};
};

class ScriptedFormCoreSession final : public service::CoreSession {
 public:
  explicit ScriptedFormCoreSession(CoreServiceManager* manager);
  ~ScriptedFormCoreSession() override;

  void Bind(mojo::PendingReceiver<service::CoreSession> receiver);
  const std::vector<service::CoreServiceCommandPtr>& commands() const {
    return commands_;
  }
  const std::optional<LiveFormApprovalTarget>& target() const {
    return target_;
  }

  void Submit(service::CoreServiceCommandPtr command,
              SubmitCallback callback) override;
  void ValidateAccountTokenResponse(
      service::AccountTokenValidationRequestPtr request,
      ValidateAccountTokenResponseCallback callback) override;
  void EvaluatePolicy(service::PolicyEvaluationRequestPtr request,
                      EvaluatePolicyCallback callback) override;
  void ExportPageSnapshot(service::PageSnapshotExportCommandPtr command,
                          ExportPageSnapshotCallback callback) override;
  void CancelPageSnapshotExport(
      service::OperationEnvelopePtr operation,
      CancelPageSnapshotExportCallback callback) override;
  void MatchSiteSkills(service::SiteSkillMatchCommandPtr command,
                       MatchSiteSkillsCallback callback) override;
  void QuerySavedFlows(service::SavedFlowQueryCommandPtr command,
                       QuerySavedFlowsCallback callback) override;
  void CompleteTaskSettlement(
      service::TaskSettlementBindingPtr settlement) override;
  void Cancel(service::OperationEnvelopePtr operation) override;
  void DeliverToolStreamChunk(service::ToolStreamChunkPtr chunk) override;
  void DeliverModelStreamChunk(
      service::ModelStreamChunkPtr chunk,
      DeliverModelStreamChunkCallback callback) override;
  void DeliverEffectResult(service::EffectResultPtr result) override;
  void PlanEntitlementRefresh(service::EntitlementFetchReason reason,
                              PlanEntitlementRefreshCallback callback) override;
  void DeliverEntitlementFetchResult(
      service::EffectResultPtr result,
      DeliverEntitlementFetchResultCallback callback) override;
  void PrepareBackupManifest(service::BackupManifestPrepareRequestPtr request,
                             PrepareBackupManifestCallback callback) override;
  void InspectBackupManifest(service::BackupManifestInspectRequestPtr request,
                             InspectBackupManifestCallback callback) override;
  void PlanBackupRestore(service::BackupRestorePlanRequestPtr request,
                         PlanBackupRestoreCallback callback) override;
  void ConfirmBackupRestorePlan(
      service::BackupRestorePlanConfirmationRequestPtr request,
      ConfirmBackupRestorePlanCallback callback) override;
  void ReportBackupRestoreStageVerified(
      service::BackupRestoreStageVerificationRequestPtr request,
      ReportBackupRestoreStageVerifiedCallback callback) override;
  void ReportBackupRestoreCommitOutcome(
      service::BackupRestoreCommitOutcomeReportPtr request,
      ReportBackupRestoreCommitOutcomeCallback callback) override;
  void ChooseBackupRestoreResolution(
      service::BackupRestoreResolutionRequestPtr request,
      ChooseBackupRestoreResolutionCallback callback) override;
  void ReportBackupRestoreResolutionOutcome(
      service::BackupRestoreResolutionOutcomeReportPtr request,
      ReportBackupRestoreResolutionOutcomeCallback callback) override;
  void CancelBackupRestoreBeforeCommit(
      service::BackupRestoreCancellationRequestPtr request,
      CancelBackupRestoreBeforeCommitCallback callback) override;
  void InspectBackupRestoreRecovery(
      service::BackupRestoreRecoveryInspectionRequestPtr request,
      InspectBackupRestoreRecoveryCallback callback) override;
  void ChooseBackupRestoreRecoveryResolution(
      service::BackupRestoreRecoveryResolutionRequestPtr request,
      ChooseBackupRestoreRecoveryResolutionCallback callback) override;
  void ReportBackupRestoreRecoveryResolutionOutcome(
      service::BackupRestoreRecoveryResolutionOutcomeReportPtr report,
      ReportBackupRestoreRecoveryResolutionOutcomeCallback callback) override;

 private:
  const raw_ptr<CoreServiceManager> manager_;
  uint64_t next_capability_ = 1u;
  std::vector<service::CoreServiceCommandPtr> commands_;
  std::optional<LiveFormApprovalTarget> target_;
  mojo::Receiver<service::CoreSession> receiver_{this};
};

// The fixture driver's lifecycle/discovery half lives in test_support.cc;
// the value/approval/action half lives in test_action.cc. Keeping the state in
// this private implementation declaration lets those two responsibilities
// share one exact authority chain without making any of it public test API.
class FormApprovalVerticalHarness::Impl final {
 public:
  Impl(CoreServiceManager* manager, content::WebContents* web_contents);
  ~Impl();

  bool Initialize();
  bool DiscoverForm();
  bool StartWebErrand(std::string source_host);
  bool OpenFieldValueRequest(std::string request_id);
  surface::FieldValueSupplyVerdict Supply(std::vector<std::string> values);
  bool WaitForSuppliedCount(uint32_t expected_count) const;
  bool ConfirmFillProposal(std::string action_id,
                           std::string proposal_digest,
                           uint32_t supplied_value_index);
  service::PolicyEvaluationStatus AuthorizeFill();
  service::TaskEffectCompletionStatus DispatchFill();
  service::TaskEffectCompletionStatus ReplayDispatch();

  size_t submitted_command_count() const;
  uint32_t last_supplied_count() const;
  bool CoreCommandsContain(std::string_view value) const;
  const LiveFormApprovalTarget& target() const;
  const std::string& task_id() const;
  const std::string& request_id() const;
  const std::vector<std::string>& field_ids() const;
  const std::vector<std::string>& field_labels() const;
  uint32_t approval_lifetime_seconds() const;

 private:
  bool RegisterAndPublish(service::CoreStateBrowserBindingsPtr bindings,
                          uint64_t sequence);
  service::TaskEffectCompletionStatus ExecuteDispatch(
      service::TaskEffectBindingPtr effect);

  const raw_ptr<CoreServiceManager> manager_;
  const raw_ptr<content::WebContents> web_contents_;
  FormVerticalObserver observer_;
  ScriptedFormCoreSession session_;
  RecordingFieldValueClient field_client_;
  mojo::Remote<surface::TaffyFieldValueSurface> surface_;
  bool observing_ = false;
  uint64_t window_token_ = 0u;
  LiveFormApprovalTarget target_;
  std::string task_id_;
  std::string request_id_;
  std::string action_id_;
  std::string proposal_digest_;
  uint32_t supplied_value_index_ = 0u;
  uint32_t approval_lifetime_seconds_ = 0u;
  std::vector<std::string> field_ids_;
  std::vector<std::string> field_labels_;
  service::CoreServiceCommandPtr start_command_;
  service::CoreServiceCommandPtr decision_command_;
  service::MintedCapabilityGrantPtr grant_;
  service::TaskEffectBindingPtr dispatch_;
};

std::vector<uint8_t> CanonicalFormFillIntent(
    const LiveFormApprovalTarget& target,
    const std::string& request_id,
    uint32_t supplied_value_index);

service::TaskExecutableActionPtr MakeFillExecutable(
    const LiveFormApprovalTarget& target,
    const std::string& request_id,
    uint32_t supplied_value_index);

service::CoreStateUpdatePtr MakeFormState(uint64_t generation,
                                          uint64_t sequence);

service::CoreStateBrowserBindingsPtr MakeEmptyFormBindings(uint64_t generation,
                                                           uint64_t sequence);

service::CoreStateBrowserBindingsPtr MakeTaskFormBindings(
    uint64_t generation,
    uint64_t sequence,
    const std::string& task_id,
    uint64_t task_revision,
    const service::StartTaskCommand& start,
    const std::optional<std::string>& pending_action_id,
    const std::optional<std::string>& pending_proposal_digest,
    const service::UserDecisionCommand* committed_decision);

service::TaskEffectBindingPtr MakeFieldValueRequestEffect(
    uint64_t generation,
    uint64_t task_revision,
    const std::string& task_id,
    const std::string& request_id,
    const LiveFormApprovalTarget& target,
    uint64_t now_monotonic_ms);

service::TaskEffectBindingPtr MakeFormApprovalEffect(
    uint64_t generation,
    uint64_t task_revision,
    const std::string& task_id,
    const std::string& action_id,
    const std::string& proposal_digest,
    service::TaskExecutableActionPtr executable,
    uint64_t now_monotonic_ms);

service::TaskPolicyEffectPtr MakeFormPolicyEffect(
    uint64_t generation,
    uint64_t task_revision,
    const std::string& task_id,
    const std::string& action_id,
    const std::string& proposal_digest,
    const std::string& request_id,
    uint32_t supplied_value_index,
    const LiveFormApprovalTarget& target,
    const service::UserDecisionCommand& decision,
    uint64_t now_monotonic_ms);

service::TaskEffectBindingPtr MakeFormDispatchEffect(
    uint64_t generation,
    uint64_t task_revision,
    const std::string& task_id,
    const std::string& action_id,
    const std::string& proposal_digest,
    const std::string& request_id,
    uint32_t supplied_value_index,
    const LiveFormApprovalTarget& target,
    const service::MintedCapabilityGrant& grant);

bool FormCommandContains(const service::CoreServiceCommand& command,
                         std::string_view value);

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_FORM_APPROVAL_VERTICAL_TEST_INTERNAL_H_
