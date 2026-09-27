// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "crypto/secure_util.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_service_manager.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

core_mojom::OperationEnvelopePtr EchoOperation(
    const core_mojom::OperationEnvelope* operation) {
  return operation ? operation->Clone() : core_mojom::OperationEnvelope::New();
}

BackupPrepareResult UnavailablePrepare(
    const core_mojom::OperationEnvelope* operation) {
  auto result = core_mojom::BackupManifestPrepareResult::New();
  result->operation = EchoOperation(operation);
  result->status = core_mojom::BackupPlanningStatus::kUnavailable;
  return result;
}

BackupInspectResult UnavailableInspect(
    const core_mojom::OperationEnvelope* operation) {
  auto result = core_mojom::BackupManifestInspectResult::New();
  result->operation = EchoOperation(operation);
  result->status = core_mojom::BackupPlanningStatus::kUnavailable;
  return result;
}

BackupRestoreResult UnavailableRestore(
    const core_mojom::OperationEnvelope* operation,
    core_mojom::BackupRestoreTargetKind target_kind) {
  auto result = core_mojom::BackupRestorePlanResult::New();
  result->operation = EchoOperation(operation);
  result->status = core_mojom::BackupPlanningStatus::kUnavailable;
  result->target = core_mojom::BackupRestoreTarget::New(target_kind, "");
  return result;
}

void WipeManifest(BackupPrepareResult* result) {
  if (*result) {
    crypto::SecureZeroBuffer((*result)->manifest_plaintext);
    (*result)->manifest_plaintext.clear();
  }
}

}  // namespace

std::optional<CoreBackupProtocol::WorkflowInterest>
CoreBackupProtocol::AcquireWorkflowInterest(base::OnceClosure on_disconnected) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  if (!on_disconnected || manager_->shutdown_started_ ||
      manager_->availability_ != CoreServiceAvailability::kReady ||
      !manager_->session_.is_bound() || next_workflow_interest_id_ == 0u) {
    return std::nullopt;
  }
  const uint64_t interest_id = next_workflow_interest_id_++;
  const bool inserted = workflow_disconnect_callbacks_
                            .emplace(interest_id, std::move(on_disconnected))
                            .second;
  CHECK(inserted);
  manager_->RefreshIdleTeardown();
  return WorkflowInterest(
      base::BindOnce(&CoreBackupProtocol::ReleaseWorkflowInterest,
                     weak_factory_.GetWeakPtr(), interest_id));
}

void CoreBackupProtocol::ReleaseWorkflowInterest(uint64_t interest_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  if (workflow_disconnect_callbacks_.erase(interest_id) == 1u) {
    manager_->RefreshIdleTeardown();
  }
}

bool CoreBackupProtocol::BeginCall(
    const core_mojom::OperationEnvelope* operation,
    bool request_is_valid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  if (manager_->shutdown_started_ ||
      manager_->availability_ != CoreServiceAvailability::kReady ||
      !manager_->session_.is_bound() || !operation || !request_is_valid ||
      pending_operation_ids_.contains(operation->operation_id)) {
    return false;
  }
  pending_operation_ids_.insert(operation->operation_id);
  manager_->RefreshIdleTeardown();
  return true;
}

bool CoreBackupProtocol::CompleteCall(
    uint64_t generation,
    const core_mojom::OperationEnvelope& expected_operation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const bool same_generation = manager_->service_generation_ == generation;
  const bool owned =
      same_generation &&
      pending_operation_ids_.erase(expected_operation.operation_id) == 1u;
  const bool current =
      owned && !manager_->shutdown_started_ &&
      manager_->availability_ == CoreServiceAvailability::kReady &&
      manager_->session_.is_bound() &&
      IsLiveBackupOperation(&expected_operation, generation,
                            BackupPlanningNowMonotonicMillis());
  if (!same_generation || !owned) {
    ++manager_->late_reply_count_;
  }
  manager_->RefreshIdleTeardown();
  return current;
}

void CoreBackupProtocol::OnServiceDisconnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  OnBackupRestoreRetirementSourceDisconnected();
  pending_operation_ids_.clear();
  auto disconnected = std::move(workflow_disconnect_callbacks_);
  workflow_disconnect_callbacks_.clear();
  for (auto& [interest_id, callback] : disconnected) {
    static_cast<void>(interest_id);
    std::move(callback).Run();
  }
}

void CoreBackupProtocol::PrepareBackupManifest(BackupPrepareRequest request,
                                               BackupPrepareCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      request ? request->operation.get() : nullptr;
  if (!request ||
      !BeginCall(operation, IsValidBackupPrepareRequest(
                                *request, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    std::move(callback).Run(UnavailablePrepare(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  const size_t record_count = request->records.size();
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  manager_->session_->PrepareBackupManifest(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation, size_t record_count,
                 core_mojom::OperationEnvelopePtr expected,
                 BackupPrepareCallback callback, BackupPrepareResult result) {
                const bool current =
                    protocol && protocol->CompleteCall(generation, *expected);
                if (!current || !result ||
                    !IsValidBackupPrepareResult(*expected, record_count,
                                                *result)) {
                  WipeManifest(&result);
                  result = UnavailablePrepare(expected.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, record_count,
              std::move(expected), std::move(callback)),
          nullptr));
}

void CoreBackupProtocol::InspectBackupManifest(BackupInspectRequest request,
                                               BackupInspectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      request ? request->operation.get() : nullptr;
  if (!request ||
      !BeginCall(operation, IsValidBackupInspectRequest(
                                *request, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    std::move(callback).Run(UnavailableInspect(operation));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  manager_->session_->InspectBackupManifest(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation, core_mojom::OperationEnvelopePtr expected,
                 BackupInspectCallback callback, BackupInspectResult result) {
                const bool current =
                    protocol && protocol->CompleteCall(generation, *expected);
                if (!current || !result ||
                    !IsValidBackupInspectResult(*expected, *result)) {
                  result = UnavailableInspect(expected.get());
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(expected),
              std::move(callback)),
          nullptr));
}

void CoreBackupProtocol::PlanBackupRestore(BackupRestoreRequest request,
                                           BackupRestoreCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  const core_mojom::OperationEnvelope* operation =
      request ? request->operation.get() : nullptr;
  const core_mojom::BackupRestoreTargetKind target_kind =
      request && request->target
          ? request->target->kind
          : core_mojom::BackupRestoreTargetKind::kNewRegularProfile;
  if (!request || BackupRestorePlanningBlockedByRetirement() ||
      !BeginCall(operation, IsValidBackupRestoreRequest(
                                *request, manager_->service_generation_,
                                BackupPlanningNowMonotonicMillis()))) {
    std::move(callback).Run(UnavailableRestore(operation, target_kind));
    return;
  }
  const uint64_t generation = manager_->service_generation_;
  std::vector<core_mojom::StagedBackupRecordPtr> expected_staged_records;
  expected_staged_records.reserve(request->staged_records.size());
  for (const auto& staged : request->staged_records) {
    expected_staged_records.push_back(staged.Clone());
  }
  core_mojom::OperationEnvelopePtr expected = operation->Clone();
  core_mojom::BackupRestoreTargetPtr expected_target = request->target.Clone();
  const std::string expected_owner_profile_id = manager_->browser_profile_id_;
  manager_->session_->PlanBackupRestore(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreBackupProtocol> protocol,
                 uint64_t generation,
                 std::vector<core_mojom::StagedBackupRecordPtr>
                     expected_staged_records,
                 core_mojom::OperationEnvelopePtr expected,
                 core_mojom::BackupRestoreTargetPtr expected_target,
                 std::string expected_owner_profile_id,
                 BackupRestoreCallback callback, BackupRestoreResult result) {
                const bool current =
                    protocol && protocol->CompleteCall(generation, *expected);
                if (!current || !result ||
                    !IsValidBackupRestoreResult(
                        *expected, expected_owner_profile_id, *expected_target,
                        expected_staged_records, *result)) {
                  result =
                      UnavailableRestore(expected.get(), expected_target->kind);
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation,
              std::move(expected_staged_records), std::move(expected),
              std::move(expected_target), expected_owner_profile_id,
              std::move(callback)),
          nullptr));
}

}  // namespace taffy
