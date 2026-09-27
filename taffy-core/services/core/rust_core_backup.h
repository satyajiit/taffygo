// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_BACKUP_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_BACKUP_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "taffy/services/core/service_bridge_backup_ffi.rs.h"

namespace taffy::core_service_internal {

bool IsValidBridgeBackupRestoreBinding(
    const core_bridge::BridgeBackupRestoreBinding& binding);
core_service::mojom::BackupRestoreBindingPtr ToMojoBackupRestoreBinding(
    const core_bridge::BridgeBackupRestoreBinding& binding);

std::optional<core_bridge::BridgeBackupManifestPrepareRequest>
ToBridgeBackupManifestPrepareRequest(
    core_service::mojom::BackupManifestPrepareRequest& request);
std::optional<core_bridge::BridgeBackupOperation> ToBridgeBackupOperation(
    const core_service::mojom::OperationEnvelope& operation);
std::optional<core_bridge::BridgeBackupRestorePlanRequest>
ToBridgeBackupRestorePlanRequest(
    core_service::mojom::BackupRestorePlanRequest& request);
std::optional<core_bridge::BridgeBackupRestorePlanConfirmationRequest>
ToBridgeBackupRestorePlanConfirmationRequest(
    const core_service::mojom::BackupRestorePlanConfirmationRequest& request);
std::optional<core_bridge::BridgeBackupRestoreStageVerificationRequest>
ToBridgeBackupRestoreStageVerificationRequest(
    const core_service::mojom::BackupRestoreStageVerificationRequest& request);
std::optional<core_bridge::BridgeBackupRestoreCommitOutcomeReport>
ToBridgeBackupRestoreCommitOutcomeReport(
    const core_service::mojom::BackupRestoreCommitOutcomeReport& report);
std::optional<core_bridge::BridgeBackupRestoreResolutionRequest>
ToBridgeBackupRestoreResolutionRequest(
    const core_service::mojom::BackupRestoreResolutionRequest& request);
std::optional<core_bridge::BridgeBackupRestoreResolutionOutcomeReport>
ToBridgeBackupRestoreResolutionOutcomeReport(
    const core_service::mojom::BackupRestoreResolutionOutcomeReport& report);
std::optional<core_bridge::BridgeBackupRestoreCancellationRequest>
ToBridgeBackupRestoreCancellationRequest(
    const core_service::mojom::BackupRestoreCancellationRequest& request);
std::optional<core_bridge::BridgeBackupRestoreRecoveryInspectionRequest>
ToBridgeBackupRestoreRecoveryInspectionRequest(
    const core_service::mojom::BackupRestoreRecoveryInspectionRequest& request);
std::optional<core_bridge::BridgeBackupRestoreRecoveryBinding>
ToBridgeBackupRestoreRecoveryBinding(
    const core_service::mojom::BackupRestoreRecoveryBinding& binding);
std::optional<core_bridge::BridgeBackupRestoreRecoveryRecord>
ToBridgeBackupRestoreRecoveryRecord(
    const core_service::mojom::BackupRestoreRecoveryRecord& record);
std::optional<core_bridge::BridgeBackupRestoreRecoveryResolutionRequest>
ToBridgeBackupRestoreRecoveryResolutionRequest(
    const core_service::mojom::BackupRestoreRecoveryResolutionRequest& request);
std::optional<core_bridge::BridgeBackupRestoreRecoveryResolutionOutcomeReport>
ToBridgeBackupRestoreRecoveryResolutionOutcomeReport(
    const core_service::mojom::BackupRestoreRecoveryResolutionOutcomeReport&
        report);

core_service::mojom::BackupManifestPrepareResultPtr
ToMojoBackupManifestPrepareResult(
    core_bridge::BridgeBackupManifestPrepareResult result);
core_service::mojom::BackupManifestInspectResultPtr
ToMojoBackupManifestInspectResult(
    core_bridge::BridgeBackupManifestInspectResult result);
core_service::mojom::BackupRestorePlanResultPtr ToMojoBackupRestorePlanResult(
    core_bridge::BridgeBackupRestorePlanResult result);
core_service::mojom::BackupRestoreStageAuthorizationResultPtr
ToMojoBackupRestoreStageAuthorizationResult(
    core_bridge::BridgeBackupRestoreStageAuthorizationResult result);
core_service::mojom::BackupRestoreCommitAuthorizationResultPtr
ToMojoBackupRestoreCommitAuthorizationResult(
    core_bridge::BridgeBackupRestoreCommitAuthorizationResult result);
core_service::mojom::BackupRestoreResolutionAuthorizationResultPtr
ToMojoBackupRestoreResolutionAuthorizationResult(
    core_bridge::BridgeBackupRestoreResolutionAuthorizationResult result);
core_service::mojom::BackupRestoreProtocolResultPtr
ToMojoBackupRestoreProtocolResult(
    core_bridge::BridgeBackupRestoreProtocolResult result);
core_service::mojom::BackupRestoreRecoveryInspectionResultPtr
ToMojoBackupRestoreRecoveryInspectionResult(
    core_bridge::BridgeBackupRestoreRecoveryInspectionResult result);
core_service::mojom::BackupRestoreRecoveryBindingPtr
ToMojoBackupRestoreRecoveryBinding(
    core_bridge::BridgeBackupRestoreRecoveryBinding binding);
core_service::mojom::BackupRestoreRecoveryRecordPtr
ToMojoBackupRestoreRecoveryRecord(
    core_bridge::BridgeBackupRestoreRecoveryRecord record);
core_service::mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
ToMojoBackupRestoreRecoveryResolutionAuthorizationResult(
    core_bridge::BridgeBackupRestoreRecoveryResolutionAuthorizationResult
        result);

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_BACKUP_H_
