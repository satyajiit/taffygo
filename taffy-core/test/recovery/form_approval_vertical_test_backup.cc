// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/test/recovery/form_approval_vertical_test_internal.h"

namespace taffy::test {
namespace {

service::OperationEnvelopePtr EchoOperation(
    const service::OperationEnvelope* operation) {
  return operation ? operation->Clone() : service::OperationEnvelope::New();
}

}  // namespace

void ScriptedFormCoreSession::PrepareBackupManifest(
    service::BackupManifestPrepareRequestPtr request,
    PrepareBackupManifestCallback callback) {
  auto result = service::BackupManifestPrepareResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupPlanningStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::InspectBackupManifest(
    service::BackupManifestInspectRequestPtr request,
    InspectBackupManifestCallback callback) {
  auto result = service::BackupManifestInspectResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupPlanningStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::PlanBackupRestore(
    service::BackupRestorePlanRequestPtr request,
    PlanBackupRestoreCallback callback) {
  auto result = service::BackupRestorePlanResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupPlanningStatus::kUnavailable;
  const service::BackupRestoreTargetKind target_kind =
      request && request->target
          ? request->target->kind
          : service::BackupRestoreTargetKind::kNewRegularProfile;
  result->target = service::BackupRestoreTarget::New(target_kind, "");
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::ConfirmBackupRestorePlan(
    service::BackupRestorePlanConfirmationRequestPtr request,
    ConfirmBackupRestorePlanCallback callback) {
  auto result = service::BackupRestoreStageAuthorizationResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupRestoreProtocolStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::ReportBackupRestoreStageVerified(
    service::BackupRestoreStageVerificationRequestPtr request,
    ReportBackupRestoreStageVerifiedCallback callback) {
  auto result = service::BackupRestoreCommitAuthorizationResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupRestoreProtocolStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::ReportBackupRestoreCommitOutcome(
    service::BackupRestoreCommitOutcomeReportPtr request,
    ReportBackupRestoreCommitOutcomeCallback callback) {
  auto result = service::BackupRestoreProtocolResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupRestoreProtocolStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::ChooseBackupRestoreResolution(
    service::BackupRestoreResolutionRequestPtr request,
    ChooseBackupRestoreResolutionCallback callback) {
  auto result = service::BackupRestoreResolutionAuthorizationResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupRestoreProtocolStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::ReportBackupRestoreResolutionOutcome(
    service::BackupRestoreResolutionOutcomeReportPtr request,
    ReportBackupRestoreResolutionOutcomeCallback callback) {
  auto result = service::BackupRestoreProtocolResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupRestoreProtocolStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::CancelBackupRestoreBeforeCommit(
    service::BackupRestoreCancellationRequestPtr request,
    CancelBackupRestoreBeforeCommitCallback callback) {
  auto result = service::BackupRestoreProtocolResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupRestoreProtocolStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::InspectBackupRestoreRecovery(
    service::BackupRestoreRecoveryInspectionRequestPtr request,
    InspectBackupRestoreRecoveryCallback callback) {
  auto result = service::BackupRestoreRecoveryInspectionResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status =
      service::BackupRestoreRecoveryInspectionStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::ChooseBackupRestoreRecoveryResolution(
    service::BackupRestoreRecoveryResolutionRequestPtr request,
    ChooseBackupRestoreRecoveryResolutionCallback callback) {
  auto result =
      service::BackupRestoreRecoveryResolutionAuthorizationResult::New();
  result->operation =
      EchoOperation(request ? request->operation.get() : nullptr);
  result->status = service::BackupRestoreProtocolStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::ReportBackupRestoreRecoveryResolutionOutcome(
    service::BackupRestoreRecoveryResolutionOutcomeReportPtr report,
    ReportBackupRestoreRecoveryResolutionOutcomeCallback callback) {
  auto result = service::BackupRestoreProtocolResult::New();
  result->operation = EchoOperation(report ? report->operation.get() : nullptr);
  result->status = service::BackupRestoreProtocolStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy::test
