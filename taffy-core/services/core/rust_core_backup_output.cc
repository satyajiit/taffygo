// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
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

template <typename Range>
bool IsAllZero(const Range& value) {
  return std::ranges::all_of(value, [](uint8_t byte) { return byte == 0; });
}

bool IsBoundedNonEmpty(const rust::String& value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

bool IsValidOperation(const bridge::BridgeBackupOperation& operation) {
  return IsBoundedNonEmpty(operation.operation_id,
                           mojom::kMaxOperationIdBytes) &&
         IsBoundedNonEmpty(operation.idempotency_key,
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

bool IsPrepareShape(const bridge::BridgeBackupManifestPrepareResult& input,
                    mojom::BackupPlanningStatus status) {
  if (status != mojom::BackupPlanningStatus::kSucceeded) {
    return input.manifest_plaintext.empty() &&
           IsAllZero(input.snapshot_sha256) &&
           input.payload_plaintext_bytes == 0u && input.source_order.empty() &&
           input.expected_sealed_chunks == 0u;
  }
  if (input.manifest_plaintext.empty() ||
      input.manifest_plaintext.size() > mojom::kMaxBackupManifestBytes ||
      IsAllZero(input.snapshot_sha256) ||
      input.payload_plaintext_bytes > mojom::kMaxBackupPlaintextBytes ||
      input.source_order.size() > mojom::kMaxBackupRecords ||
      input.expected_sealed_chunks == 0u ||
      input.expected_sealed_chunks > mojom::kMaxBackupSealedChunks) {
    return false;
  }
  const uint64_t payload_chunks =
      input.payload_plaintext_bytes == 0u
          ? 0u
          : 1u + ((input.payload_plaintext_bytes - 1u) /
                  mojom::kBackupPayloadChunkBytes);
  if (input.expected_sealed_chunks != payload_chunks + 1u) {
    return false;
  }
  std::vector<bool> seen(input.source_order.size());
  for (const uint32_t ordinal : input.source_order) {
    if (ordinal >= seen.size() || seen[ordinal]) {
      return false;
    }
    seen[ordinal] = true;
  }
  return true;
}

bool IsInspectShape(const bridge::BridgeBackupManifestInspectResult& input,
                    mojom::BackupPlanningStatus status) {
  if (status != mojom::BackupPlanningStatus::kSucceeded) {
    return input.backup_id.empty() && input.source_installation_id.empty() &&
           input.created_at_utc.empty() && input.selection.empty() &&
           input.record_count == 0u && IsAllZero(input.snapshot_sha256) &&
           input.payload_plaintext_bytes == 0u && input.records.empty();
  }
  if (!IsBoundedNonEmpty(input.backup_id, mojom::kMaxBackupIdBytes) ||
      !IsBoundedNonEmpty(input.source_installation_id,
                         mojom::kMaxBackupIdBytes) ||
      input.created_at_utc.empty() ||
      input.created_at_utc.size() > mojom::kMaxBackupTimestampBytes ||
      input.selection.empty() ||
      input.selection.size() > mojom::kMaxBackupSelectionKinds ||
      input.record_count != input.records.size() ||
      input.records.size() > mojom::kMaxBackupRecords ||
      IsAllZero(input.snapshot_sha256) ||
      input.payload_plaintext_bytes > mojom::kMaxBackupPlaintextBytes) {
    return false;
  }
  std::optional<uint8_t> previous_kind;
  for (const uint8_t kind : input.selection) {
    if (!wire::BackupRecordKindFromWire(kind) ||
        (previous_kind && *previous_kind >= kind)) {
      return false;
    }
    previous_kind = kind;
  }
  uint64_t total = 0;
  for (const bridge::BridgeBackupPayloadLayoutEntry& record : input.records) {
    const std::optional<mojom::BackupRecordState> state =
        wire::BackupRecordStateFromWire(record.state);
    if (!state || record.plaintext_bytes > mojom::kMaxBackupRecordBytes ||
        (*state == mojom::BackupRecordState::kActive &&
         record.plaintext_bytes == 0u) ||
        (*state == mojom::BackupRecordState::kTombstone &&
         record.plaintext_bytes != 0u) ||
        record.plaintext_bytes > mojom::kMaxBackupPlaintextBytes - total) {
      return false;
    }
    total += record.plaintext_bytes;
  }
  return total == input.payload_plaintext_bytes;
}

bool IsRestoreShape(const bridge::BridgeBackupRestorePlanResult& input,
                    mojom::BackupPlanningStatus status) {
  if (!wire::BackupRestoreTargetKindFromWire(input.target_kind)) {
    return false;
  }
  if (status != mojom::BackupPlanningStatus::kSucceeded) {
    return input.backup_id.empty() && IsAllZero(input.snapshot_sha256) &&
           input.target_profile_id.empty() && input.entries.empty() &&
           !input.has_conflicts && IsAllZero(input.confirmation_sha256) &&
           !input.has_binding;
  }
  if (!IsBoundedNonEmpty(input.backup_id, mojom::kMaxBackupIdBytes) ||
      IsAllZero(input.snapshot_sha256) ||
      !IsBoundedNonEmpty(input.target_profile_id, mojom::kMaxBackupIdBytes) ||
      input.entries.size() > mojom::kMaxBackupRecords ||
      IsAllZero(input.confirmation_sha256) || !input.has_binding ||
      !IsValidBridgeBackupRestoreBinding(input.binding) ||
      !SameOperation(input.operation, input.binding.planning_operation) ||
      input.binding.backup_id != input.backup_id ||
      input.binding.target_kind != input.target_kind ||
      input.binding.target_profile_id != input.target_profile_id ||
      !std::ranges::equal(input.binding.snapshot_sha256,
                          input.snapshot_sha256) ||
      !std::ranges::equal(input.binding.confirmation_sha256,
                          input.confirmation_sha256)) {
    return false;
  }
  std::optional<std::pair<uint8_t, std::string>> previous;
  bool found_conflict = false;
  uint64_t total_bytes = 0u;
  for (const bridge::BridgeBackupRestorePlanEntry& entry : input.entries) {
    const std::optional<mojom::BackupRestoreAction> action =
        wire::BackupRestoreActionFromWire(entry.action);
    const std::optional<mojom::BackupRecordState> state =
        wire::BackupRecordStateFromWire(entry.state);
    const bool zero_digest = IsAllZero(entry.plaintext_sha256);
    if (!wire::BackupRecordKindFromWire(entry.kind) || !action || !state ||
        !IsBoundedNonEmpty(entry.stable_id, mojom::kMaxBackupIdBytes) ||
        entry.archive_revision == 0u || entry.schema_version == 0u ||
        entry.plaintext_bytes > mojom::kMaxBackupRecordBytes ||
        (*state == mojom::BackupRecordState::kActive &&
         (entry.plaintext_bytes == 0u || zero_digest)) ||
        (*state == mojom::BackupRecordState::kTombstone &&
         (entry.plaintext_bytes != 0u || !zero_digest)) ||
        entry.plaintext_bytes > mojom::kMaxBackupPlaintextBytes - total_bytes) {
      return false;
    }
    total_bytes += entry.plaintext_bytes;
    std::pair<uint8_t, std::string> identity(entry.kind,
                                             std::string(entry.stable_id));
    if (previous && *previous >= identity) {
      return false;
    }
    previous = std::move(identity);
    found_conflict |=
        *action == mojom::BackupRestoreAction::kBlockedByDeletion ||
        *action == mojom::BackupRestoreAction::kNeedsExplicitConflictChoice;
  }
  return input.has_conflicts == found_conflict;
}

mojom::OperationEnvelopePtr ToMojoOperation(
    const bridge::BridgeBackupOperation& input) {
  return mojom::OperationEnvelope::New(
      std::string(input.operation_id), input.service_generation,
      input.task_revision, input.deadline_monotonic_ms,
      std::string(input.idempotency_key));
}

}  // namespace

mojom::BackupManifestPrepareResultPtr ToMojoBackupManifestPrepareResult(
    bridge::BridgeBackupManifestPrepareResult input) {
  const std::optional<mojom::BackupPlanningStatus> status =
      wire::BackupPlanningStatusFromWire(input.status);
  if (!status || !IsValidOperation(input.operation) ||
      !IsPrepareShape(input, *status)) {
    return nullptr;
  }
  auto output = mojom::BackupManifestPrepareResult::New();
  output->operation = ToMojoOperation(input.operation);
  output->status = *status;
  output->manifest_plaintext.assign(input.manifest_plaintext.begin(),
                                    input.manifest_plaintext.end());
  output->snapshot_sha256.assign(input.snapshot_sha256.begin(),
                                 input.snapshot_sha256.end());
  output->payload_plaintext_bytes = input.payload_plaintext_bytes;
  output->source_order.assign(input.source_order.begin(),
                              input.source_order.end());
  output->expected_sealed_chunks = input.expected_sealed_chunks;
  return output;
}

mojom::BackupManifestInspectResultPtr ToMojoBackupManifestInspectResult(
    bridge::BridgeBackupManifestInspectResult input) {
  const std::optional<mojom::BackupPlanningStatus> status =
      wire::BackupPlanningStatusFromWire(input.status);
  if (!status || !IsValidOperation(input.operation) ||
      !IsInspectShape(input, *status)) {
    return nullptr;
  }
  auto output = mojom::BackupManifestInspectResult::New();
  output->operation = ToMojoOperation(input.operation);
  output->status = *status;
  output->backup_id = std::string(input.backup_id);
  output->source_installation_id = std::string(input.source_installation_id);
  output->created_at_utc = std::string(input.created_at_utc);
  for (const uint8_t kind : input.selection) {
    output->selection.push_back(*wire::BackupRecordKindFromWire(kind));
  }
  output->record_count = input.record_count;
  output->snapshot_sha256.assign(input.snapshot_sha256.begin(),
                                 input.snapshot_sha256.end());
  output->payload_plaintext_bytes = input.payload_plaintext_bytes;
  output->records.reserve(input.records.size());
  for (const bridge::BridgeBackupPayloadLayoutEntry& record : input.records) {
    output->records.push_back(mojom::BackupPayloadLayoutEntry::New(
        *wire::BackupRecordStateFromWire(record.state),
        record.plaintext_bytes));
  }
  return output;
}

mojom::BackupRestorePlanResultPtr ToMojoBackupRestorePlanResult(
    bridge::BridgeBackupRestorePlanResult input) {
  const std::optional<mojom::BackupPlanningStatus> status =
      wire::BackupPlanningStatusFromWire(input.status);
  if (!status || !IsValidOperation(input.operation) ||
      !IsRestoreShape(input, *status)) {
    return nullptr;
  }
  auto output = mojom::BackupRestorePlanResult::New();
  output->operation = ToMojoOperation(input.operation);
  output->status = *status;
  output->backup_id = std::string(input.backup_id);
  output->snapshot_sha256.assign(input.snapshot_sha256.begin(),
                                 input.snapshot_sha256.end());
  output->target = mojom::BackupRestoreTarget::New(
      *wire::BackupRestoreTargetKindFromWire(input.target_kind),
      std::string(input.target_profile_id));
  output->entries.reserve(input.entries.size());
  for (const bridge::BridgeBackupRestorePlanEntry& entry : input.entries) {
    output->entries.push_back(mojom::BackupRestorePlanEntry::New(
        *wire::BackupRecordKindFromWire(entry.kind),
        std::string(entry.stable_id), entry.archive_revision,
        *wire::BackupRestoreActionFromWire(entry.action), entry.schema_version,
        *wire::BackupRecordStateFromWire(entry.state), entry.plaintext_bytes,
        std::vector<uint8_t>(entry.plaintext_sha256.begin(),
                             entry.plaintext_sha256.end())));
  }
  output->has_conflicts = input.has_conflicts;
  output->confirmation_sha256.assign(input.confirmation_sha256.begin(),
                                     input.confirmation_sha256.end());
  if (input.has_binding) {
    output->binding = ToMojoBackupRestoreBinding(input.binding);
    if (!output->binding) {
      return nullptr;
    }
  }
  return output;
}

}  // namespace taffy::core_service_internal
