// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_BACKUP_PROTOCOL_VALIDATION_H_
#define TAFFY_BROWSER_CORE_BACKUP_PROTOCOL_VALIDATION_H_

#include <cstdint>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Structural validation for the portable restore authority projected by the
// source Core. A binding's original planning deadline is immutable identity,
// not the lifetime of later review or hidden staging.
bool IsValidBackupRestoreBinding(
    const core_service::mojom::BackupRestoreBinding* binding,
    uint64_t service_generation);
bool IsExactBackupRestoreBinding(
    const core_service::mojom::BackupRestoreBinding* expected,
    const core_service::mojom::BackupRestoreBinding* received);

bool IsValidBackupRestorePlanConfirmationRequest(
    const core_service::mojom::BackupRestorePlanConfirmationRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsValidBackupRestoreStageVerificationRequest(
    const core_service::mojom::BackupRestoreStageVerificationRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsValidBackupRestoreCommitOutcomeReport(
    const core_service::mojom::BackupRestoreCommitOutcomeReport& report,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsValidBackupRestoreResolutionRequest(
    const core_service::mojom::BackupRestoreResolutionRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsValidBackupRestoreResolutionOutcomeReport(
    const core_service::mojom::BackupRestoreResolutionOutcomeReport& report,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsValidBackupRestoreCancellationRequest(
    const core_service::mojom::BackupRestoreCancellationRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);

bool IsValidBackupRestoreStageAuthorizationResult(
    const core_service::mojom::OperationEnvelope& expected_operation,
    const core_service::mojom::BackupRestoreBinding& expected_binding,
    const core_service::mojom::BackupRestoreStageAuthorizationResult& result);
bool IsValidBackupRestoreCommitAuthorizationResult(
    const core_service::mojom::OperationEnvelope& expected_operation,
    const core_service::mojom::BackupRestoreBinding& expected_binding,
    const core_service::mojom::BackupRestoreCommitAuthorizationResult& result);
bool IsValidBackupRestoreResolutionAuthorizationResult(
    const core_service::mojom::OperationEnvelope& expected_operation,
    const core_service::mojom::BackupRestoreBinding& expected_binding,
    core_service::mojom::BackupRestoreResolutionChoice expected_choice,
    const core_service::mojom::BackupRestoreResolutionAuthorizationResult&
        result);
bool IsValidBackupRestoreProtocolResult(
    const core_service::mojom::OperationEnvelope& expected_operation,
    const core_service::mojom::BackupRestoreProtocolResult& result);

// Physical commit/publication/deletion must call these immediately before it
// starts. Outcome reports intentionally use a fresh operation and remain
// valid after the issued authorization's deadline.
bool IsLiveBackupRestoreCommitAuthorization(
    const core_service::mojom::BackupRestoreCommitAuthorization* authorization,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsLiveBackupRestoreResolutionAuthorization(
    const core_service::mojom::BackupRestoreResolutionAuthorization*
        authorization,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_BACKUP_PROTOCOL_VALIDATION_H_
