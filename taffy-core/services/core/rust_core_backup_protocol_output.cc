// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>

#include "base/strings/string_util.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_backup.h"

namespace taffy::core_service_internal {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;
namespace wire = core_service::wire;

bool IsBoundedText(const rust::String& input, uint64_t maximum) {
  const std::string value(input);
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

template <typename Range>
bool IsNonZeroDigest(const Range& value) {
  return std::ranges::any_of(value, [](uint8_t byte) { return byte != 0u; });
}

bool IsValidOperation(const bridge::BridgeBackupOperation& operation) {
  return IsBoundedText(operation.operation_id, mojom::kMaxOperationIdBytes) &&
         IsBoundedText(operation.idempotency_key,
                       mojom::kMaxIdempotencyKeyBytes);
}

bool SameOperation(const bridge::BridgeBackupOperation& left,
                   const bridge::BridgeBackupOperation& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

mojom::OperationEnvelopePtr ToOperation(
    const bridge::BridgeBackupOperation& input) {
  return mojom::OperationEnvelope::New(
      std::string(input.operation_id), input.service_generation,
      input.task_revision, input.deadline_monotonic_ms,
      std::string(input.idempotency_key));
}

std::optional<mojom::BackupRestoreProtocolStatus> Status(uint8_t value) {
  return wire::BackupRestoreProtocolStatusFromWire(value);
}

bool HasAuthority(mojom::BackupRestoreProtocolStatus status) {
  return status == mojom::BackupRestoreProtocolStatus::kSucceeded;
}

}  // namespace

bool IsValidBridgeBackupRestoreBinding(
    const bridge::BridgeBackupRestoreBinding& input) {
  const std::optional<mojom::BackupRestoreTargetKind> target_kind =
      wire::BackupRestoreTargetKindFromWire(input.target_kind);
  return IsValidOperation(input.planning_operation) &&
         IsBoundedText(input.owner_profile_id, mojom::kMaxBackupIdBytes) &&
         target_kind == mojom::BackupRestoreTargetKind::kNewRegularProfile &&
         IsBoundedText(input.target_profile_id, mojom::kMaxBackupIdBytes) &&
         input.owner_profile_id != input.target_profile_id &&
         IsBoundedText(input.backup_id, mojom::kMaxBackupIdBytes) &&
         IsNonZeroDigest(input.snapshot_sha256) &&
         IsNonZeroDigest(input.confirmation_sha256);
}

mojom::BackupRestoreBindingPtr ToMojoBackupRestoreBinding(
    const bridge::BridgeBackupRestoreBinding& input) {
  if (!IsValidBridgeBackupRestoreBinding(input)) {
    return nullptr;
  }
  auto output = mojom::BackupRestoreBinding::New();
  output->planning_operation = ToOperation(input.planning_operation);
  output->owner_profile_id = std::string(input.owner_profile_id);
  output->target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile,
      std::string(input.target_profile_id));
  output->backup_id = std::string(input.backup_id);
  output->snapshot_sha256.assign(input.snapshot_sha256.begin(),
                                 input.snapshot_sha256.end());
  output->confirmation_sha256.assign(input.confirmation_sha256.begin(),
                                     input.confirmation_sha256.end());
  return output;
}

mojom::BackupRestoreStageAuthorizationResultPtr
ToMojoBackupRestoreStageAuthorizationResult(
    bridge::BridgeBackupRestoreStageAuthorizationResult input) {
  const std::optional<mojom::BackupRestoreProtocolStatus> status =
      Status(input.status);
  if (!status || !IsValidOperation(input.operation) ||
      input.has_authorization != HasAuthority(*status)) {
    return nullptr;
  }
  auto output = mojom::BackupRestoreStageAuthorizationResult::New();
  output->operation = ToOperation(input.operation);
  output->status = *status;
  if (!input.has_authorization) {
    return output;
  }
  if (!IsValidBridgeBackupRestoreBinding(input.authorization.binding) ||
      !SameOperation(input.operation, input.authorization.decision_operation)) {
    return nullptr;
  }
  output->authorization = mojom::BackupRestoreStageAuthorization::New(
      ToMojoBackupRestoreBinding(input.authorization.binding),
      ToOperation(input.authorization.decision_operation));
  return output;
}

mojom::BackupRestoreCommitAuthorizationResultPtr
ToMojoBackupRestoreCommitAuthorizationResult(
    bridge::BridgeBackupRestoreCommitAuthorizationResult input) {
  const std::optional<mojom::BackupRestoreProtocolStatus> status =
      Status(input.status);
  if (!status || !IsValidOperation(input.operation) ||
      input.has_authorization != HasAuthority(*status)) {
    return nullptr;
  }
  auto output = mojom::BackupRestoreCommitAuthorizationResult::New();
  output->operation = ToOperation(input.operation);
  output->status = *status;
  if (!input.has_authorization) {
    return output;
  }
  if (!IsValidBridgeBackupRestoreBinding(input.authorization.binding) ||
      !SameOperation(input.operation, input.authorization.decision_operation)) {
    return nullptr;
  }
  output->authorization = mojom::BackupRestoreCommitAuthorization::New(
      ToMojoBackupRestoreBinding(input.authorization.binding),
      ToOperation(input.authorization.decision_operation));
  return output;
}

mojom::BackupRestoreResolutionAuthorizationResultPtr
ToMojoBackupRestoreResolutionAuthorizationResult(
    bridge::BridgeBackupRestoreResolutionAuthorizationResult input) {
  const std::optional<mojom::BackupRestoreProtocolStatus> status =
      Status(input.status);
  const std::optional<mojom::BackupRestoreResolutionChoice> choice =
      wire::BackupRestoreResolutionChoiceFromWire(input.authorization.choice);
  if (!status || !IsValidOperation(input.operation) ||
      input.has_authorization != HasAuthority(*status) || !choice) {
    return nullptr;
  }
  auto output = mojom::BackupRestoreResolutionAuthorizationResult::New();
  output->operation = ToOperation(input.operation);
  output->status = *status;
  if (!input.has_authorization) {
    return output;
  }
  if (!IsValidBridgeBackupRestoreBinding(input.authorization.binding) ||
      !SameOperation(input.operation, input.authorization.decision_operation)) {
    return nullptr;
  }
  output->authorization = mojom::BackupRestoreResolutionAuthorization::New(
      ToMojoBackupRestoreBinding(input.authorization.binding),
      ToOperation(input.authorization.decision_operation), *choice);
  return output;
}

mojom::BackupRestoreProtocolResultPtr ToMojoBackupRestoreProtocolResult(
    bridge::BridgeBackupRestoreProtocolResult input) {
  const std::optional<mojom::BackupRestoreProtocolStatus> status =
      Status(input.status);
  if (!status || !IsValidOperation(input.operation)) {
    return nullptr;
  }
  return mojom::BackupRestoreProtocolResult::New(ToOperation(input.operation),
                                                 *status);
}

}  // namespace taffy::core_service_internal
