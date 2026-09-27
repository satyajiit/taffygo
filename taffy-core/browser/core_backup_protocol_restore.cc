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
#include "taffy/browser/core_service_manager.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

core_mojom::OperationEnvelopePtr EchoOperation(
    const core_mojom::OperationEnvelope* operation) {
  return operation ? operation->Clone() : core_mojom::OperationEnvelope::New();
}

core_mojom::BackupRestoreStageAuthorizationResultPtr UnavailableStage(
    const core_mojom::OperationEnvelope* operation) {
  auto result = core_mojom::BackupRestoreStageAuthorizationResult::New();
  result->operation = EchoOperation(operation);
  result->status = core_mojom::BackupRestoreProtocolStatus::kUnavailable;
  return result;
}

core_mojom::BackupRestoreCommitAuthorizationResultPtr UnavailableCommit(
    const core_mojom::OperationEnvelope* operation) {
  auto result = core_mojom::BackupRestoreCommitAuthorizationResult::New();
  result->operation = EchoOperation(operation);
  result->status = core_mojom::BackupRestoreProtocolStatus::kUnavailable;
  return result;
}

core_mojom::BackupRestoreResolutionAuthorizationResultPtr UnavailableResolution(
    const core_mojom::OperationEnvelope* operation) {
  auto result = core_mojom::BackupRestoreResolutionAuthorizationResult::New();
  result->operation = EchoOperation(operation);
  result->status = core_mojom::BackupRestoreProtocolStatus::kUnavailable;
  return result;
}

core_mojom::BackupRestoreProtocolResultPtr UnavailableProtocol(
    const core_mojom::OperationEnvelope* operation) {
  return core_mojom::BackupRestoreProtocolResult::New(
      EchoOperation(operation),
      core_mojom::BackupRestoreProtocolStatus::kUnavailable);
}

}  // namespace

void CoreBackupProtocol::ConfirmBackupRestorePlan(
    core_mojom::BackupRestorePlanConfirmationRequestPtr request,
    StageAuthorizationCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      request ? request->operation.get() : nullptr;
  if (!request ||
      !BeginCall(operation, IsValidBackupRestorePlanConfirmationRequest(
                                *request, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    std::move(callback).Run(UnavailableStage(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  core_mojom::BackupRestoreBindingPtr expected_binding =
      request->binding.Clone();
  manager_->session_->ConfirmBackupRestorePlan(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation, core_mojom::OperationEnvelopePtr expected,
                 core_mojom::BackupRestoreBindingPtr expected_binding,
                 StageAuthorizationCallback callback,
                 core_mojom::BackupRestoreStageAuthorizationResultPtr result) {
                const bool current =
                    protocol && protocol->CompleteCall(generation, *expected);
                if (!current || !result ||
                    !IsValidBackupRestoreStageAuthorizationResult(
                        *expected, *expected_binding, *result)) {
                  result = UnavailableStage(expected.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(expected_binding), std::move(callback)),
          nullptr));
}

void CoreBackupProtocol::ReportBackupRestoreStageVerified(
    core_mojom::BackupRestoreStageVerificationRequestPtr request,
    CommitAuthorizationCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      request ? request->operation.get() : nullptr;
  if (!request ||
      !BeginCall(operation, IsValidBackupRestoreStageVerificationRequest(
                                *request, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    std::move(callback).Run(UnavailableCommit(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  core_mojom::BackupRestoreBindingPtr expected_binding =
      request->authorization->binding.Clone();
  manager_->session_->ReportBackupRestoreStageVerified(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation, core_mojom::OperationEnvelopePtr expected,
                 core_mojom::BackupRestoreBindingPtr expected_binding,
                 CommitAuthorizationCallback callback,
                 core_mojom::BackupRestoreCommitAuthorizationResultPtr result) {
                const bool current =
                    protocol && protocol->CompleteCall(generation, *expected);
                if (!current || !result ||
                    !IsValidBackupRestoreCommitAuthorizationResult(
                        *expected, *expected_binding, *result)) {
                  result = UnavailableCommit(expected.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(expected_binding), std::move(callback)),
          nullptr));
}

void CoreBackupProtocol::ReportBackupRestoreCommitOutcome(
    core_mojom::BackupRestoreCommitOutcomeReportPtr report,
    ProtocolCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      report ? report->operation.get() : nullptr;
  if (!report ||
      !BeginCall(operation, IsValidBackupRestoreCommitOutcomeReport(
                                *report, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    std::move(callback).Run(UnavailableProtocol(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  manager_->session_->ReportBackupRestoreCommitOutcome(
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
                  result = UnavailableProtocol(expected.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(callback)),
          nullptr));
}

void CoreBackupProtocol::ChooseBackupRestoreResolution(
    core_mojom::BackupRestoreResolutionRequestPtr request,
    ResolutionAuthorizationCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      request ? request->operation.get() : nullptr;
  if (!request ||
      !BeginCall(operation, IsValidBackupRestoreResolutionRequest(
                                *request, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    std::move(callback).Run(UnavailableResolution(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  const core_mojom::BackupRestoreResolutionChoice expected_choice =
      request->choice;
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  core_mojom::BackupRestoreBindingPtr expected_binding =
      request->binding.Clone();
  manager_->session_->ChooseBackupRestoreResolution(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation, core_mojom::OperationEnvelopePtr expected,
                 core_mojom::BackupRestoreBindingPtr expected_binding,
                 core_mojom::BackupRestoreResolutionChoice expected_choice,
                 ResolutionAuthorizationCallback callback,
                 core_mojom::BackupRestoreResolutionAuthorizationResultPtr
                     result) {
                const bool current =
                    protocol && protocol->CompleteCall(generation, *expected);
                if (!current || !result ||
                    !IsValidBackupRestoreResolutionAuthorizationResult(
                        *expected, *expected_binding, expected_choice,
                        *result)) {
                  result = UnavailableResolution(expected.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(expected_binding), expected_choice,
              std::move(callback)),
          nullptr));
}

void CoreBackupProtocol::ReportBackupRestoreResolutionOutcome(
    core_mojom::BackupRestoreResolutionOutcomeReportPtr report,
    ProtocolCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      report ? report->operation.get() : nullptr;
  if (!report ||
      !BeginCall(operation, IsValidBackupRestoreResolutionOutcomeReport(
                                *report, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    std::move(callback).Run(UnavailableProtocol(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  manager_->session_->ReportBackupRestoreResolutionOutcome(
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
                  result = UnavailableProtocol(expected.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(callback)),
          nullptr));
}

void CoreBackupProtocol::CancelBackupRestoreBeforeCommit(
    core_mojom::BackupRestoreCancellationRequestPtr request,
    ProtocolCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      request ? request->operation.get() : nullptr;
  core_mojom::BackupRestoreBindingPtr observed_binding =
      request && request->binding ? request->binding.Clone() : nullptr;
  if (!request ||
      !BeginCall(operation, IsValidBackupRestoreCancellationRequest(
                                *request, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    core_mojom::BackupRestoreProtocolResultPtr result =
        UnavailableProtocol(operation);
    ObserveBackupRestoreCancellationRejected(observed_binding.get());
    std::move(callback).Run(std::move(result));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  core_mojom::BackupRestoreBindingPtr expected_binding =
      request->binding.Clone();
  ObserveBackupRestoreCancellationAdmitted(*expected, *expected_binding);
  manager_->session_->CancelBackupRestoreBeforeCommit(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation, core_mojom::OperationEnvelopePtr expected,
                 core_mojom::BackupRestoreBindingPtr expected_binding,
                 ProtocolCallback callback,
                 core_mojom::BackupRestoreProtocolResultPtr result) {
                const bool current =
                    protocol && protocol->CompleteCall(generation, *expected);
                if (!current || !result ||
                    !IsValidBackupRestoreProtocolResult(*expected, *result)) {
                  result = UnavailableProtocol(expected.get());
                }
                if (protocol) {
                  protocol->ObserveBackupRestoreCancellationCompleted(
                      expected->operation_id, *expected_binding,
                      result->status);
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(expected_binding), std::move(callback)),
          nullptr));
}

}  // namespace taffy
