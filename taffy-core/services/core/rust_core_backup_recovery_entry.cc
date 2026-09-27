// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/rust_core_backup.h"
#include "taffy/services/core/rust_core_bridge_handle.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace core_mojom = core_service::mojom;

bool SameOperation(const core_mojom::OperationEnvelope& left,
                   const core_mojom::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

core_mojom::BackupRestoreRecoveryInspectionResultPtr Refused(
    const core_mojom::OperationEnvelope& operation,
    core_mojom::BackupRestoreRecoveryInspectionStatus status) {
  return core_mojom::BackupRestoreRecoveryInspectionResult::New(
      operation.Clone(), status, nullptr, nullptr);
}

}  // namespace

core_mojom::BackupRestoreRecoveryInspectionResultPtr
RustCore::InspectBackupRestoreRecovery(
    core_mojom::BackupRestoreRecoveryInspectionRequestPtr request,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation) {
    return nullptr;
  }
  const core_mojom::OperationEnvelopePtr expected = request->operation.Clone();
  if (!core_service_internal::ToBridgeBackupOperation(*request->operation)) {
    return Refused(
        *expected,
        core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidOperation);
  }
  auto projected =
      core_service_internal::ToBridgeBackupRestoreRecoveryInspectionRequest(
          *request);
  if (!projected) {
    return Refused(
        *expected,
        core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidRecord);
  }
  auto result =
      core_service_internal::ToMojoBackupRestoreRecoveryInspectionResult(
          bridge::InspectBackupRestoreRecovery(
              *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && result->operation &&
                 SameOperation(*result->operation, *expected)
             ? std::move(result)
             : nullptr;
}

}  // namespace taffy
