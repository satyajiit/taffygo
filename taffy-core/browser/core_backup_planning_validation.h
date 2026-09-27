// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_BACKUP_PLANNING_VALIDATION_H_
#define TAFFY_BROWSER_CORE_BACKUP_PLANNING_VALIDATION_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Browser-side validation around the sandboxed canonical backup planner. It
// deliberately knows only typed descriptors and bounded planner output. It
// never parses manifest plaintext or accepts record payload bytes.
uint64_t BackupPlanningNowMonotonicMillis();

bool IsLiveBackupOperation(
    const core_service::mojom::OperationEnvelope* operation,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsExactBackupOperation(
    const core_service::mojom::OperationEnvelope* expected,
    const core_service::mojom::OperationEnvelope* received);

bool IsValidBackupPrepareRequest(
    const core_service::mojom::BackupManifestPrepareRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsValidBackupInspectRequest(
    const core_service::mojom::BackupManifestInspectRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);
bool IsValidBackupRestoreRequest(
    const core_service::mojom::BackupRestorePlanRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms);

bool IsValidBackupPrepareResult(
    const core_service::mojom::OperationEnvelope& expected_operation,
    size_t expected_record_count,
    const core_service::mojom::BackupManifestPrepareResult& result);
bool IsValidBackupInspectResult(
    const core_service::mojom::OperationEnvelope& expected_operation,
    const core_service::mojom::BackupManifestInspectResult& result);
bool IsValidBackupRestoreResult(
    const core_service::mojom::OperationEnvelope& expected_operation,
    const std::string& expected_owner_profile_id,
    const core_service::mojom::BackupRestoreTarget& expected_target,
    const std::vector<core_service::mojom::StagedBackupRecordPtr>&
        expected_staged_records,
    const core_service::mojom::BackupRestorePlanResult& result);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_BACKUP_PLANNING_VALIDATION_H_
