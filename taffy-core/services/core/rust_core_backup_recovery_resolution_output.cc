// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_backup.h"

namespace taffy::core_service_internal {
namespace {

namespace bridge = core_bridge;
namespace core_mojom = core_service::mojom;
namespace wire = core_service::wire;

constexpr size_t kRecoveryResolutionSuffixCapacity = 3u;

bool IsBoundedText(const rust::String& input, uint64_t maximum) {
  const std::string value(input);
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

core_mojom::OperationEnvelopePtr ToOperation(
    const bridge::BridgeBackupOperation& input) {
  if (!IsBoundedText(input.operation_id, core_mojom::kMaxOperationIdBytes) ||
      !IsBoundedText(input.idempotency_key,
                     core_mojom::kMaxIdempotencyKeyBytes)) {
    return nullptr;
  }
  return core_mojom::OperationEnvelope::New(
      std::string(input.operation_id), input.service_generation,
      input.task_revision, input.deadline_monotonic_ms,
      std::string(input.idempotency_key));
}

core_mojom::BackupRestoreRecoveryResolutionAuthorizationPtr ToAuthorization(
    bridge::BridgeBackupRestoreRecoveryResolutionAuthorization input) {
  const auto choice = wire::BackupRestoreResolutionChoiceFromWire(input.choice);
  auto binding = ToMojoBackupRestoreRecoveryBinding(std::move(input.binding));
  auto operation = ToOperation(input.decision_operation);
  if (!choice || !binding || !operation ||
      !IsBoundedText(input.intent_id, core_mojom::kMaxBackupIdBytes) ||
      input.history_prefix.size() >
          core_mojom::kMaxBackupRestoreRecoveryRecords -
              kRecoveryResolutionSuffixCapacity) {
    return nullptr;
  }
  std::vector<core_mojom::BackupRestoreRecoveryRecordPtr> prefix;
  prefix.reserve(input.history_prefix.size());
  for (auto& record : input.history_prefix) {
    auto projected = ToMojoBackupRestoreRecoveryRecord(std::move(record));
    if (!projected) {
      return nullptr;
    }
    prefix.push_back(std::move(projected));
  }
  return core_mojom::BackupRestoreRecoveryResolutionAuthorization::New(
      std::move(binding), std::move(operation), *choice,
      std::string(input.intent_id), std::move(prefix));
}

}  // namespace

core_mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
ToMojoBackupRestoreRecoveryResolutionAuthorizationResult(
    bridge::BridgeBackupRestoreRecoveryResolutionAuthorizationResult input) {
  const auto status = wire::BackupRestoreProtocolStatusFromWire(input.status);
  const auto choice =
      wire::BackupRestoreResolutionChoiceFromWire(input.authorization.choice);
  auto operation = ToOperation(input.operation);
  if (!status || !choice || !operation ||
      input.has_authorization !=
          (*status == core_mojom::BackupRestoreProtocolStatus::kSucceeded)) {
    return nullptr;
  }
  core_mojom::BackupRestoreRecoveryResolutionAuthorizationPtr authorization;
  if (input.has_authorization) {
    authorization = ToAuthorization(std::move(input.authorization));
    if (!authorization) {
      return nullptr;
    }
  }
  return core_mojom::BackupRestoreRecoveryResolutionAuthorizationResult::New(
      std::move(operation), *status, std::move(authorization));
}

}  // namespace taffy::core_service_internal
