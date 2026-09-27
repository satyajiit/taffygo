// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/rust_core_backup.h"
#include "taffy/services/core/rust_core_bridge_handle.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_backup_ffi.rs.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

bool MatchesOperation(const mojom::OperationEnvelope* operation,
                      const std::string& operation_id,
                      uint64_t generation,
                      const std::string& idempotency_key) {
  return operation && operation->operation_id == operation_id &&
         operation->service_generation == generation &&
         operation->idempotency_key == idempotency_key;
}

}  // namespace

mojom::BackupManifestPrepareResultPtr RustCore::PrepareBackupManifest(
    mojom::BackupManifestPrepareRequestPtr request,
    uint64_t expected_generation,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation) {
    return nullptr;
  }
  const std::string operation_id = request->operation->operation_id;
  const std::string idempotency_key = request->operation->idempotency_key;
  std::optional<bridge::BridgeBackupManifestPrepareRequest> projected =
      core_service_internal::ToBridgeBackupManifestPrepareRequest(*request);
  if (!projected) {
    return nullptr;
  }
  mojom::BackupManifestPrepareResultPtr result =
      core_service_internal::ToMojoBackupManifestPrepareResult(
          bridge::PrepareBackupManifest(std::move(*projected),
                                        expected_generation, now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    expected_generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

mojom::BackupManifestInspectResultPtr RustCore::InspectBackupManifest(
    mojom::BackupManifestInspectRequestPtr request,
    uint64_t expected_generation,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation ||
      request->manifest_plaintext.empty()) {
    return nullptr;
  }
  const std::string operation_id = request->operation->operation_id;
  const std::string idempotency_key = request->operation->idempotency_key;
  std::optional<bridge::BridgeBackupOperation> operation =
      core_service_internal::ToBridgeBackupOperation(*request->operation);
  if (!operation ||
      request->manifest_plaintext.size() > mojom::kMaxBackupManifestBytes) {
    return nullptr;
  }
  const std::vector<uint8_t>& manifest = request->manifest_plaintext;
  const rust::Slice<const uint8_t> borrowed_manifest(manifest.data(),
                                                     manifest.size());
  mojom::BackupManifestInspectResultPtr result =
      core_service_internal::ToMojoBackupManifestInspectResult(
          bridge::InspectBackupManifest(std::move(*operation),
                                        borrowed_manifest, expected_generation,
                                        now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    expected_generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

mojom::BackupRestorePlanResultPtr RustCore::PlanBackupRestore(
    mojom::BackupRestorePlanRequestPtr request,
    uint64_t expected_generation,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation ||
      request->manifest_plaintext.empty()) {
    return nullptr;
  }
  const std::string operation_id = request->operation->operation_id;
  const std::string idempotency_key = request->operation->idempotency_key;
  std::optional<bridge::BridgeBackupRestorePlanRequest> projected =
      core_service_internal::ToBridgeBackupRestorePlanRequest(*request);
  if (!projected) {
    return nullptr;
  }
  const std::vector<uint8_t>& manifest = request->manifest_plaintext;
  const rust::Slice<const uint8_t> borrowed_manifest(manifest.data(),
                                                     manifest.size());
  mojom::BackupRestorePlanResultPtr result =
      core_service_internal::ToMojoBackupRestorePlanResult(
          bridge::PlanBackupRestore(*bridge_->runtime(), std::move(*projected),
                                    borrowed_manifest, now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    expected_generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

mojom::BackupRestoreStageAuthorizationResultPtr
RustCore::ConfirmBackupRestorePlan(
    mojom::BackupRestorePlanConfirmationRequestPtr request,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation) {
    return nullptr;
  }
  const std::string operation_id = request->operation->operation_id;
  const uint64_t generation = request->operation->service_generation;
  const std::string idempotency_key = request->operation->idempotency_key;
  auto projected =
      core_service_internal::ToBridgeBackupRestorePlanConfirmationRequest(
          *request);
  if (!projected) {
    return nullptr;
  }
  auto result =
      core_service_internal::ToMojoBackupRestoreStageAuthorizationResult(
          bridge::ConfirmBackupRestorePlan(
              *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

mojom::BackupRestoreCommitAuthorizationResultPtr
RustCore::ReportBackupRestoreStageVerified(
    mojom::BackupRestoreStageVerificationRequestPtr request,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation) {
    return nullptr;
  }
  const std::string operation_id = request->operation->operation_id;
  const uint64_t generation = request->operation->service_generation;
  const std::string idempotency_key = request->operation->idempotency_key;
  auto projected =
      core_service_internal::ToBridgeBackupRestoreStageVerificationRequest(
          *request);
  if (!projected) {
    return nullptr;
  }
  auto result =
      core_service_internal::ToMojoBackupRestoreCommitAuthorizationResult(
          bridge::ReportBackupRestoreStageVerified(
              *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

mojom::BackupRestoreProtocolResultPtr
RustCore::ReportBackupRestoreCommitOutcome(
    mojom::BackupRestoreCommitOutcomeReportPtr report,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !report || !report->operation) {
    return nullptr;
  }
  const std::string operation_id = report->operation->operation_id;
  const uint64_t generation = report->operation->service_generation;
  const std::string idempotency_key = report->operation->idempotency_key;
  auto projected =
      core_service_internal::ToBridgeBackupRestoreCommitOutcomeReport(*report);
  if (!projected) {
    return nullptr;
  }
  auto result = core_service_internal::ToMojoBackupRestoreProtocolResult(
      bridge::ReportBackupRestoreCommitOutcome(
          *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

mojom::BackupRestoreResolutionAuthorizationResultPtr
RustCore::ChooseBackupRestoreResolution(
    mojom::BackupRestoreResolutionRequestPtr request,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation) {
    return nullptr;
  }
  const std::string operation_id = request->operation->operation_id;
  const uint64_t generation = request->operation->service_generation;
  const std::string idempotency_key = request->operation->idempotency_key;
  auto projected =
      core_service_internal::ToBridgeBackupRestoreResolutionRequest(*request);
  if (!projected) {
    return nullptr;
  }
  auto result =
      core_service_internal::ToMojoBackupRestoreResolutionAuthorizationResult(
          bridge::ChooseBackupRestoreResolution(
              *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

mojom::BackupRestoreProtocolResultPtr
RustCore::ReportBackupRestoreResolutionOutcome(
    mojom::BackupRestoreResolutionOutcomeReportPtr report,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !report || !report->operation) {
    return nullptr;
  }
  const std::string operation_id = report->operation->operation_id;
  const uint64_t generation = report->operation->service_generation;
  const std::string idempotency_key = report->operation->idempotency_key;
  auto projected =
      core_service_internal::ToBridgeBackupRestoreResolutionOutcomeReport(
          *report);
  if (!projected) {
    return nullptr;
  }
  auto result = core_service_internal::ToMojoBackupRestoreProtocolResult(
      bridge::ReportBackupRestoreResolutionOutcome(
          *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

mojom::BackupRestoreProtocolResultPtr RustCore::CancelBackupRestoreBeforeCommit(
    mojom::BackupRestoreCancellationRequestPtr request,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !request || !request->operation) {
    return nullptr;
  }
  const std::string operation_id = request->operation->operation_id;
  const uint64_t generation = request->operation->service_generation;
  const std::string idempotency_key = request->operation->idempotency_key;
  auto projected =
      core_service_internal::ToBridgeBackupRestoreCancellationRequest(*request);
  if (!projected) {
    return nullptr;
  }
  auto result = core_service_internal::ToMojoBackupRestoreProtocolResult(
      bridge::CancelBackupRestoreBeforeCommit(
          *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  return result && MatchesOperation(result->operation.get(), operation_id,
                                    generation, idempotency_key)
             ? std::move(result)
             : nullptr;
}

}  // namespace taffy
