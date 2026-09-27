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

core_mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
RefusedAuthorization(const core_mojom::OperationEnvelope& operation,
                     core_mojom::BackupRestoreProtocolStatus status) {
  return core_mojom::BackupRestoreRecoveryResolutionAuthorizationResult::New(
      operation.Clone(), status, nullptr);
}

core_mojom::BackupRestoreProtocolResultPtr RefusedReport(
    const core_mojom::OperationEnvelope& operation,
    core_mojom::BackupRestoreProtocolStatus status) {
  return core_mojom::BackupRestoreProtocolResult::New(operation.Clone(),
                                                      status);
}

}  // namespace

core_mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
RustCore::ChooseBackupRestoreRecoveryResolution(
    core_mojom::BackupRestoreRecoveryResolutionRequestPtr request,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation) {
    return nullptr;
  }
  const auto expected = request->operation.Clone();
  if (!core_service_internal::ToBridgeBackupOperation(*request->operation)) {
    return RefusedAuthorization(
        *expected, core_mojom::BackupRestoreProtocolStatus::kInvalidOperation);
  }
  auto projected =
      core_service_internal::ToBridgeBackupRestoreRecoveryResolutionRequest(
          *request);
  if (!projected) {
    return RefusedAuthorization(
        *expected, core_mojom::BackupRestoreProtocolStatus::kBindingMismatch);
  }
  auto result = core_service_internal::
      ToMojoBackupRestoreRecoveryResolutionAuthorizationResult(
          bridge::ChooseBackupRestoreRecoveryResolution(
              *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && result->operation &&
                 SameOperation(*result->operation, *expected)
             ? std::move(result)
             : nullptr;
}

core_mojom::BackupRestoreProtocolResultPtr
RustCore::ReportBackupRestoreRecoveryResolutionOutcome(
    core_mojom::BackupRestoreRecoveryResolutionOutcomeReportPtr report,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !report || !report->operation) {
    return nullptr;
  }
  const auto expected = report->operation.Clone();
  if (!core_service_internal::ToBridgeBackupOperation(*report->operation)) {
    return RefusedReport(
        *expected, core_mojom::BackupRestoreProtocolStatus::kInvalidOperation);
  }
  auto projected = core_service_internal::
      ToBridgeBackupRestoreRecoveryResolutionOutcomeReport(*report);
  if (!projected) {
    return RefusedReport(
        *expected, core_mojom::BackupRestoreProtocolStatus::kBindingMismatch);
  }
  auto result = core_service_internal::ToMojoBackupRestoreProtocolResult(
      bridge::ReportBackupRestoreRecoveryResolutionOutcome(
          *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && result->operation &&
                 SameOperation(*result->operation, *expected)
             ? std::move(result)
             : nullptr;
}

}  // namespace taffy
