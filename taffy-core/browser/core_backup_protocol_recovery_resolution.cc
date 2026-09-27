// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_backup_protocol_validation.h"
#include "taffy/browser/core_backup_recovery_validation.h"
#include "taffy/browser/core_service_manager.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

core_mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
UnavailableAuthorization(const core_mojom::OperationEnvelope* operation) {
  return core_mojom::BackupRestoreRecoveryResolutionAuthorizationResult::New(
      operation ? operation->Clone() : core_mojom::OperationEnvelope::New(),
      core_mojom::BackupRestoreProtocolStatus::kUnavailable, nullptr);
}

core_mojom::BackupRestoreProtocolResultPtr UnavailableResult(
    const core_mojom::OperationEnvelope* operation) {
  return core_mojom::BackupRestoreProtocolResult::New(
      operation ? operation->Clone() : core_mojom::OperationEnvelope::New(),
      core_mojom::BackupRestoreProtocolStatus::kUnavailable);
}

}  // namespace

void CoreBackupProtocol::ChooseBackupRestoreRecoveryResolution(
    core_mojom::BackupRestoreRecoveryResolutionRequestPtr request,
    RecoveryResolutionAuthorizationCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      request ? request->operation.get() : nullptr;
  if (!request ||
      !BeginCall(operation, IsValidBackupRestoreRecoveryResolutionRequest(
                                *request, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis(),
                                manager_->browser_profile_id_))) {
    std::move(callback).Run(UnavailableAuthorization(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  auto expected = request.Clone();
  manager_->session_->ChooseBackupRestoreRecoveryResolution(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation,
                 core_mojom::BackupRestoreRecoveryResolutionRequestPtr expected,
                 RecoveryResolutionAuthorizationCallback callback,
                 core_mojom::
                     BackupRestoreRecoveryResolutionAuthorizationResultPtr
                         result) {
                const bool current =
                    protocol &&
                    protocol->CompleteCall(generation, *expected->operation);
                if (!current || !result ||
                    !IsValidBackupRestoreRecoveryResolutionAuthorizationResult(
                        *expected, *result)) {
                  result = UnavailableAuthorization(expected->operation.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(callback)),
          nullptr));
}

void CoreBackupProtocol::ReportBackupRestoreRecoveryResolutionOutcome(
    core_mojom::BackupRestoreRecoveryResolutionOutcomeReportPtr report,
    ProtocolCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      report ? report->operation.get() : nullptr;
  if (!report ||
      !BeginCall(operation, IsValidBackupRestoreRecoveryResolutionOutcomeReport(
                                *report, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis(),
                                manager_->browser_profile_id_))) {
    std::move(callback).Run(UnavailableResult(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  auto expected = report->operation.Clone();
  manager_->session_->ReportBackupRestoreRecoveryResolutionOutcome(
      std::move(report),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation, core_mojom::OperationEnvelopePtr expected,
                 ProtocolCallback callback,
                 core_mojom::BackupRestoreProtocolResultPtr result) {
                const bool current =
                    protocol && protocol->CompleteCall(generation, *expected);
                if (!current || !result ||
                    !IsValidBackupRestoreProtocolResult(*expected, *result)) {
                  result = UnavailableResult(expected.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(callback)),
          nullptr));
}

}  // namespace taffy
