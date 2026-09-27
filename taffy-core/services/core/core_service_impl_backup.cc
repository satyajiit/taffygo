// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::OperationEnvelopePtr EchoOperation(
    const mojom::OperationEnvelope* operation) {
  return operation ? operation->Clone() : mojom::OperationEnvelope::New();
}

}  // namespace

template <typename Result>
void CoreServiceImpl::OnBackupProtocolReply(
    mojom::OperationEnvelopePtr operation,
    base::OnceCallback<void(mojo::StructPtr<Result>)> callback,
    mojo::StructPtr<Result> result) {
  // Generated responses are non-nullable. A refused CXX conversion must not
  // serialize null and tear down the unrelated session. A queued decision
  // also cannot escape after this service has begun withdrawing authority.
  if (!ready_ || shutdown_started_ || !operation ||
      operation->service_generation != generation_ || !result ||
      !result->operation || !result->operation->Equals(*operation)) {
    result = Result::New();
    result->operation = EchoOperation(operation.get());
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
  }
  std::move(callback).Run(std::move(result));
}

template <typename Result>
void CoreServiceImpl::OnBackupPlanningReply(
    mojom::OperationEnvelopePtr operation,
    base::OnceCallback<void(mojo::StructPtr<Result>)> callback,
    mojo::StructPtr<Result> result) {
  // Same reason as OnBackupProtocolReply above, for the planning half: every
  // RustCore planning entry answers a refused conversion, a refused shape or a
  // mismatched operation echo with nullptr, and these responses are
  // non-nullable. Forwarding the nullptr fails receive-side validation and
  // raises an error on the CoreSession pipe, which ends the whole utility
  // generation — every task and every pending storage effect — for a request
  // that should have come back as one closed refusal.
  if (!ready_ || shutdown_started_ || !operation ||
      operation->service_generation != generation_ || !result ||
      !result->operation || !result->operation->Equals(*operation)) {
    result = Result::New();
    result->operation = EchoOperation(operation.get());
    result->status = mojom::BackupPlanningStatus::kUnavailable;
  }
  std::move(callback).Run(std::move(result));
}

void CoreServiceImpl::OnBackupRestorePlanReply(
    mojom::OperationEnvelopePtr operation,
    mojom::BackupRestoreTargetKind target_kind,
    PlanBackupRestoreCallback callback,
    mojom::BackupRestorePlanResultPtr result) {
  // `target` is the second non-nullable struct member, so a result that
  // arrives without one is as unsendable as no result at all and is refused
  // here rather than at the receiver.
  if (!ready_ || shutdown_started_ || !operation ||
      operation->service_generation != generation_ || !result ||
      !result->operation || !result->operation->Equals(*operation) ||
      !result->target) {
    result = mojom::BackupRestorePlanResult::New();
    result->operation = EchoOperation(operation.get());
    result->status = mojom::BackupPlanningStatus::kUnavailable;
    result->target = mojom::BackupRestoreTarget::New(target_kind, "");
  }
  std::move(callback).Run(std::move(result));
}

void CoreServiceImpl::PrepareBackupManifest(
    mojom::BackupManifestPrepareRequestPtr request,
    PrepareBackupManifestCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::BackupManifestPrepareResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status = mojom::BackupPlanningStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  core_.AsyncCall(&RustCore::PrepareBackupManifest)
      .WithArgs(std::move(request), generation_, NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupPlanningReply<
                               mojom::BackupManifestPrepareResult>,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

void CoreServiceImpl::InspectBackupManifest(
    mojom::BackupManifestInspectRequestPtr request,
    InspectBackupManifestCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::BackupManifestInspectResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status = mojom::BackupPlanningStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  core_.AsyncCall(&RustCore::InspectBackupManifest)
      .WithArgs(std::move(request), generation_, NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupPlanningReply<
                               mojom::BackupManifestInspectResult>,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

void CoreServiceImpl::PlanBackupRestore(
    mojom::BackupRestorePlanRequestPtr request,
    PlanBackupRestoreCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::BackupRestorePlanResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status = mojom::BackupPlanningStatus::kUnavailable;
    const mojom::BackupRestoreTargetKind target_kind =
        request && request->target
            ? request->target->kind
            : mojom::BackupRestoreTargetKind::kNewRegularProfile;
    result->target = mojom::BackupRestoreTarget::New(target_kind, "");
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  const mojom::BackupRestoreTargetKind requested_kind =
      request->target ? request->target->kind
                      : mojom::BackupRestoreTargetKind::kNewRegularProfile;
  core_.AsyncCall(&RustCore::PlanBackupRestore)
      .WithArgs(std::move(request), generation_, NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupRestorePlanReply,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           requested_kind, std::move(callback)));
}

void CoreServiceImpl::ConfirmBackupRestorePlan(
    mojom::BackupRestorePlanConfirmationRequestPtr request,
    ConfirmBackupRestorePlanCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::BackupRestoreStageAuthorizationResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  core_.AsyncCall(&RustCore::ConfirmBackupRestorePlan)
      .WithArgs(std::move(request), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupProtocolReply<
                               mojom::BackupRestoreStageAuthorizationResult>,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

void CoreServiceImpl::ReportBackupRestoreStageVerified(
    mojom::BackupRestoreStageVerificationRequestPtr request,
    ReportBackupRestoreStageVerifiedCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::BackupRestoreCommitAuthorizationResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  core_.AsyncCall(&RustCore::ReportBackupRestoreStageVerified)
      .WithArgs(std::move(request), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupProtocolReply<
                               mojom::BackupRestoreCommitAuthorizationResult>,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

void CoreServiceImpl::ReportBackupRestoreCommitOutcome(
    mojom::BackupRestoreCommitOutcomeReportPtr report,
    ReportBackupRestoreCommitOutcomeCallback callback) {
  if (!ready_ || shutdown_started_ || !report || !report->operation ||
      report->operation->service_generation != generation_) {
    auto result = mojom::BackupRestoreProtocolResult::New();
    result->operation =
        EchoOperation(report ? report->operation.get() : nullptr);
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = report->operation.Clone();
  core_.AsyncCall(&RustCore::ReportBackupRestoreCommitOutcome)
      .WithArgs(std::move(report), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupProtocolReply<
                               mojom::BackupRestoreProtocolResult>,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

void CoreServiceImpl::ChooseBackupRestoreResolution(
    mojom::BackupRestoreResolutionRequestPtr request,
    ChooseBackupRestoreResolutionCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::BackupRestoreResolutionAuthorizationResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  core_.AsyncCall(&RustCore::ChooseBackupRestoreResolution)
      .WithArgs(std::move(request), NowMonotonicMillis())
      .Then(
          base::BindOnce(&CoreServiceImpl::OnBackupProtocolReply<
                             mojom::BackupRestoreResolutionAuthorizationResult>,
                         weak_factory_.GetWeakPtr(), std::move(operation),
                         std::move(callback)));
}

void CoreServiceImpl::ReportBackupRestoreResolutionOutcome(
    mojom::BackupRestoreResolutionOutcomeReportPtr report,
    ReportBackupRestoreResolutionOutcomeCallback callback) {
  if (!ready_ || shutdown_started_ || !report || !report->operation ||
      report->operation->service_generation != generation_) {
    auto result = mojom::BackupRestoreProtocolResult::New();
    result->operation =
        EchoOperation(report ? report->operation.get() : nullptr);
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = report->operation.Clone();
  core_.AsyncCall(&RustCore::ReportBackupRestoreResolutionOutcome)
      .WithArgs(std::move(report), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupProtocolReply<
                               mojom::BackupRestoreProtocolResult>,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

void CoreServiceImpl::CancelBackupRestoreBeforeCommit(
    mojom::BackupRestoreCancellationRequestPtr request,
    CancelBackupRestoreBeforeCommitCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::BackupRestoreProtocolResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  core_.AsyncCall(&RustCore::CancelBackupRestoreBeforeCommit)
      .WithArgs(std::move(request), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupProtocolReply<
                               mojom::BackupRestoreProtocolResult>,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

void CoreServiceImpl::InspectBackupRestoreRecovery(
    mojom::BackupRestoreRecoveryInspectionRequestPtr request,
    InspectBackupRestoreRecoveryCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::BackupRestoreRecoveryInspectionResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status =
        mojom::BackupRestoreRecoveryInspectionStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  core_.AsyncCall(&RustCore::InspectBackupRestoreRecovery)
      .WithArgs(std::move(request), NowMonotonicMillis())
      .Then(base::BindOnce(
          &CoreServiceImpl::OnBackupRecoveryInspectionReply,
          weak_factory_.GetWeakPtr(), std::move(operation),
          std::move(callback)));
}

void CoreServiceImpl::ChooseBackupRestoreRecoveryResolution(
    mojom::BackupRestoreRecoveryResolutionRequestPtr request,
    ChooseBackupRestoreRecoveryResolutionCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result =
        mojom::BackupRestoreRecoveryResolutionAuthorizationResult::New();
    result->operation =
        EchoOperation(request ? request->operation.get() : nullptr);
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = request->operation.Clone();
  core_.AsyncCall(&RustCore::ChooseBackupRestoreRecoveryResolution)
      .WithArgs(std::move(request), NowMonotonicMillis())
      .Then(base::BindOnce(
          &CoreServiceImpl::OnBackupProtocolReply<
              mojom::BackupRestoreRecoveryResolutionAuthorizationResult>,
          weak_factory_.GetWeakPtr(), std::move(operation),
          std::move(callback)));
}

void CoreServiceImpl::ReportBackupRestoreRecoveryResolutionOutcome(
    mojom::BackupRestoreRecoveryResolutionOutcomeReportPtr report,
    ReportBackupRestoreRecoveryResolutionOutcomeCallback callback) {
  if (!ready_ || shutdown_started_ || !report || !report->operation ||
      report->operation->service_generation != generation_) {
    auto result = mojom::BackupRestoreProtocolResult::New();
    result->operation =
        EchoOperation(report ? report->operation.get() : nullptr);
    result->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = report->operation.Clone();
  core_.AsyncCall(&RustCore::ReportBackupRestoreRecoveryResolutionOutcome)
      .WithArgs(std::move(report), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnBackupProtocolReply<
                               mojom::BackupRestoreProtocolResult>,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

void CoreServiceImpl::OnBackupRecoveryInspectionReply(
    mojom::OperationEnvelopePtr operation,
    InspectBackupRestoreRecoveryCallback callback,
    mojom::BackupRestoreRecoveryInspectionResultPtr result) {
  if (!ready_ || shutdown_started_ || !operation ||
      operation->service_generation != generation_ || !result ||
      !result->operation || !result->operation->Equals(*operation)) {
    result = mojom::BackupRestoreRecoveryInspectionResult::New();
    result->operation = EchoOperation(operation.get());
    result->status =
        mojom::BackupRestoreRecoveryInspectionStatus::kUnavailable;
  }
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy
