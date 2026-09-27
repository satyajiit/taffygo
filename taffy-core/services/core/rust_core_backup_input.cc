// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_backup.h"

namespace taffy::core_service_internal {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;
namespace wire = core_service::wire;

bool IsBoundedNonEmpty(const std::string& value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

template <typename Range>
bool IsAllZero(const Range& value) {
  return std::ranges::all_of(value, [](uint8_t byte) { return byte == 0; });
}

std::optional<uint8_t> KindWire(mojom::BackupRecordKind kind) {
  const uint32_t value = static_cast<uint32_t>(kind);
  return wire::BackupRecordKindFromWire(value)
             ? std::optional<uint8_t>(static_cast<uint8_t>(value))
             : std::nullopt;
}

std::optional<uint8_t> StateWire(mojom::BackupRecordState state) {
  const uint32_t value = static_cast<uint32_t>(state);
  return wire::BackupRecordStateFromWire(value)
             ? std::optional<uint8_t>(static_cast<uint8_t>(value))
             : std::nullopt;
}

bool IsValidOperation(const mojom::OperationEnvelope* operation) {
  return operation &&
         IsBoundedNonEmpty(operation->operation_id,
                           mojom::kMaxOperationIdBytes) &&
         IsBoundedNonEmpty(operation->idempotency_key,
                           mojom::kMaxIdempotencyKeyBytes);
}

bool IsValidRecords(
    const std::vector<mojom::BackupRecordDescriptorPtr>& records,
    const std::array<bool, 8>* selected) {
  if (records.size() > mojom::kMaxBackupRecords) {
    return false;
  }
  uint64_t total = 0;
  for (const auto& record : records) {
    if (!record ||
        !IsBoundedNonEmpty(record->stable_id, mojom::kMaxBackupIdBytes) ||
        record->revision == 0u || record->schema_version == 0u ||
        record->plaintext_bytes > mojom::kMaxBackupRecordBytes ||
        record->plaintext_sha256.size() != 32u) {
      return false;
    }
    const std::optional<uint8_t> kind = KindWire(record->kind);
    const std::optional<uint8_t> state = StateWire(record->state);
    if (!kind || !state ||
        (selected && !(*selected)[static_cast<size_t>(*kind)])) {
      return false;
    }
    const bool active = record->state == mojom::BackupRecordState::kActive;
    if ((active && (record->plaintext_bytes == 0u ||
                    IsAllZero(record->plaintext_sha256))) ||
        (!active && (record->plaintext_bytes != 0u ||
                     !IsAllZero(record->plaintext_sha256)))) {
      return false;
    }
    if (record->plaintext_bytes > mojom::kMaxBackupPlaintextBytes - total) {
      return false;
    }
    total += record->plaintext_bytes;
  }
  return true;
}

bool IsValidPrepare(const mojom::BackupManifestPrepareRequest& request,
                    std::array<bool, 8>* selected) {
  if (!IsValidOperation(request.operation.get()) ||
      !IsBoundedNonEmpty(request.backup_id, mojom::kMaxBackupIdBytes) ||
      !IsBoundedNonEmpty(request.source_installation_id,
                         mojom::kMaxBackupIdBytes) ||
      request.created_at_utc.empty() ||
      request.created_at_utc.size() > mojom::kMaxBackupTimestampBytes ||
      request.selection.empty() ||
      request.selection.size() > mojom::kMaxBackupSelectionKinds) {
    return false;
  }
  for (const mojom::BackupRecordKind kind : request.selection) {
    const std::optional<uint8_t> value = KindWire(kind);
    if (!value || (*selected)[static_cast<size_t>(*value)]) {
      return false;
    }
    (*selected)[static_cast<size_t>(*value)] = true;
  }
  return IsValidRecords(request.records, selected);
}

bool IsValidPlan(const mojom::BackupRestorePlanRequest& request) {
  if (!IsValidOperation(request.operation.get()) || !request.target ||
      request.manifest_plaintext.empty() ||
      request.manifest_plaintext.size() > mojom::kMaxBackupManifestBytes ||
      !IsBoundedNonEmpty(request.target->profile_id,
                         mojom::kMaxBackupIdBytes) ||
      request.staged_records.size() > mojom::kMaxBackupRecords ||
      !wire::BackupRestoreTargetKindFromWire(
          static_cast<uint32_t>(request.target->kind)) ||
      !IsValidRecords(request.current_records, nullptr)) {
    return false;
  }
  uint64_t total = 0;
  for (const auto& record : request.staged_records) {
    if (!record || record->plaintext_bytes > mojom::kMaxBackupRecordBytes ||
        record->plaintext_bytes > mojom::kMaxBackupPlaintextBytes - total ||
        record->plaintext_sha256.size() != 32u ||
        (record->plaintext_bytes == 0u &&
         !IsAllZero(record->plaintext_sha256)) ||
        (record->plaintext_bytes != 0u &&
         IsAllZero(record->plaintext_sha256))) {
      return false;
    }
    total += record->plaintext_bytes;
  }
  return true;
}

bridge::BridgeBackupRecordDescriptor ToRecord(
    mojom::BackupRecordDescriptorPtr input) {
  bridge::BridgeBackupRecordDescriptor output;
  output.kind = static_cast<uint8_t>(input->kind);
  output.stable_id = std::move(input->stable_id);
  output.revision = input->revision;
  output.schema_version = input->schema_version;
  output.state = static_cast<uint8_t>(input->state);
  output.plaintext_bytes = input->plaintext_bytes;
  std::ranges::copy(input->plaintext_sha256, output.plaintext_sha256.begin());
  return output;
}

}  // namespace

std::optional<core_bridge::BridgeBackupOperation> ToBridgeBackupOperation(
    const core_service::mojom::OperationEnvelope& input) {
  if (!IsValidOperation(&input)) {
    return std::nullopt;
  }
  bridge::BridgeBackupOperation output;
  output.operation_id = input.operation_id;
  output.service_generation = input.service_generation;
  output.task_revision = input.task_revision;
  output.deadline_monotonic_ms = input.deadline_monotonic_ms;
  output.idempotency_key = input.idempotency_key;
  return output;
}

std::optional<bridge::BridgeBackupManifestPrepareRequest>
ToBridgeBackupManifestPrepareRequest(
    mojom::BackupManifestPrepareRequest& input) {
  std::array<bool, 8> selected{};
  if (!IsValidPrepare(input, &selected)) {
    return std::nullopt;
  }
  std::optional<bridge::BridgeBackupOperation> operation =
      ToBridgeBackupOperation(*input.operation);
  if (!operation) {
    return std::nullopt;
  }
  bridge::BridgeBackupManifestPrepareRequest output;
  output.operation = std::move(*operation);
  output.backup_id = std::move(input.backup_id);
  output.source_installation_id = std::move(input.source_installation_id);
  output.created_at_utc = std::move(input.created_at_utc);
  output.selection.reserve(input.selection.size());
  for (const mojom::BackupRecordKind kind : input.selection) {
    output.selection.push_back(static_cast<uint8_t>(kind));
  }
  output.records.reserve(input.records.size());
  for (auto& record : input.records) {
    output.records.push_back(ToRecord(std::move(record)));
  }
  return output;
}

std::optional<bridge::BridgeBackupRestorePlanRequest>
ToBridgeBackupRestorePlanRequest(mojom::BackupRestorePlanRequest& input) {
  if (!IsValidPlan(input)) {
    return std::nullopt;
  }
  std::optional<bridge::BridgeBackupOperation> operation =
      ToBridgeBackupOperation(*input.operation);
  if (!operation) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestorePlanRequest output;
  output.operation = std::move(*operation);
  output.target_kind = static_cast<uint8_t>(input.target->kind);
  output.target_profile_id = std::move(input.target->profile_id);
  output.staged_records.reserve(input.staged_records.size());
  for (const auto& record : input.staged_records) {
    bridge::BridgeStagedBackupRecord staged;
    staged.plaintext_bytes = record->plaintext_bytes;
    std::ranges::copy(record->plaintext_sha256,
                      staged.plaintext_sha256.begin());
    output.staged_records.push_back(std::move(staged));
  }
  output.current_records.reserve(input.current_records.size());
  for (auto& record : input.current_records) {
    output.current_records.push_back(ToRecord(std::move(record)));
  }
  return output;
}

}  // namespace taffy::core_service_internal
