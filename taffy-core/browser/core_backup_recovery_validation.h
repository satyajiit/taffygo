// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_BACKUP_RECOVERY_VALIDATION_H_
#define TAFFY_BROWSER_CORE_BACKUP_RECOVERY_VALIDATION_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

bool IsValidBackupRestoreRecoveryBindingShape(
    const core_service::mojom::BackupRestoreRecoveryBinding* binding,
    std::string_view expected_owner_profile_id);
bool IsExactBackupRestoreRecoveryBinding(
    const core_service::mojom::BackupRestoreRecoveryBinding* expected,
    const core_service::mojom::BackupRestoreRecoveryBinding* received);
bool IsValidBackupRestoreRecoveryRecordShape(
    const core_service::mojom::BackupRestoreRecoveryRecord* record,
    std::string_view expected_owner_profile_id);
bool IsExactBackupRestoreRecoveryHistory(
    const std::vector<core_service::mojom::BackupRestoreRecoveryRecordPtr>&
        expected,
    const std::vector<core_service::mojom::BackupRestoreRecoveryRecordPtr>&
        received);

// Validates only the bounded generated transport shape and current source
// ownership. Sequence, binding equality, and recovery-state reduction remain
// solely in the portable storage domain.
bool IsValidBackupRestoreRecoveryInspectionRequest(
    const core_service::mojom::BackupRestoreRecoveryInspectionRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::string_view expected_owner_profile_id);

// Ensures a reply echoes the exact live call and has a closed observational
// shape. No classification returned here is an action authorization.
bool IsValidBackupRestoreRecoveryInspectionResult(
    const core_service::mojom::OperationEnvelope& expected_operation,
    const core_service::mojom::BackupRestoreRecoveryInspectionResult& result);

bool IsValidBackupRestoreRecoveryResolutionRequest(
    const core_service::mojom::BackupRestoreRecoveryResolutionRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::string_view expected_owner_profile_id);
bool IsValidBackupRestoreRecoveryResolutionAuthorizationResult(
    const core_service::mojom::BackupRestoreRecoveryResolutionRequest& request,
    const core_service::mojom::
        BackupRestoreRecoveryResolutionAuthorizationResult& result);
bool IsValidBackupRestoreRecoveryResolutionOutcomeReport(
    const core_service::mojom::BackupRestoreRecoveryResolutionOutcomeReport&
        report,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::string_view expected_owner_profile_id);

// Called immediately before appending and dispatching the authorized physical
// intent. Reporting may outlive this operation and therefore uses the
// structural report validator above instead.
bool IsLiveBackupRestoreRecoveryResolutionAuthorization(
    const core_service::mojom::BackupRestoreRecoveryResolutionAuthorization*
        authorization,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::string_view expected_owner_profile_id);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_BACKUP_RECOVERY_VALIDATION_H_
