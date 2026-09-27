// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_backup_planning_validation.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "base/time/time.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

template <typename Range>
bool IsAllZero(const Range& value) {
  return std::ranges::all_of(value, [](uint8_t byte) { return byte == 0u; });
}

bool IsBoundedText(std::string_view value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

std::optional<size_t> KindIndex(mojom::BackupRecordKind kind) {
  switch (kind) {
    case mojom::BackupRecordKind::kAssistantConfiguration:
      return 0u;
    case mojom::BackupRecordKind::kSavedWorkspace:
      return 1u;
    case mojom::BackupRecordKind::kLibraryEntry:
      return 2u;
    case mojom::BackupRecordKind::kMemoryRecord:
      return 3u;
    case mojom::BackupRecordKind::kUserAuthoredSkill:
      return 4u;
    case mojom::BackupRecordKind::kLearnedProcedure:
      return 5u;
    case mojom::BackupRecordKind::kBookmark:
      return 6u;
    case mojom::BackupRecordKind::kBrowserPreference:
      return 7u;
  }
  return std::nullopt;
}

bool IsKnownStatus(mojom::BackupPlanningStatus status) {
  switch (status) {
    case mojom::BackupPlanningStatus::kSucceeded:
    case mojom::BackupPlanningStatus::kInvalidRequest:
    case mojom::BackupPlanningStatus::kInvalidManifest:
    case mojom::BackupPlanningStatus::kSnapshotMismatch:
    case mojom::BackupPlanningStatus::kStagedPayloadMismatch:
    case mojom::BackupPlanningStatus::kRestoreConflict:
    case mojom::BackupPlanningStatus::kDigestUnavailable:
    case mojom::BackupPlanningStatus::kUnavailable:
      return true;
  }
  return false;
}

bool IsKnownTarget(mojom::BackupRestoreTargetKind kind) {
  switch (kind) {
    case mojom::BackupRestoreTargetKind::kNewRegularProfile:
    case mojom::BackupRestoreTargetKind::kExistingRegularProfile:
      return true;
  }
  return false;
}

bool IsKnownRecordState(mojom::BackupRecordState state) {
  switch (state) {
    case mojom::BackupRecordState::kActive:
    case mojom::BackupRecordState::kTombstone:
      return true;
  }
  return false;
}

bool IsValidSelection(const std::vector<mojom::BackupRecordKind>& selection,
                      std::array<bool, 8>* selected) {
  if (selection.empty() || selection.size() > mojom::kMaxBackupSelectionKinds) {
    return false;
  }
  selected->fill(false);
  for (const mojom::BackupRecordKind kind : selection) {
    const std::optional<size_t> index = KindIndex(kind);
    if (!index || (*selected)[*index]) {
      return false;
    }
    (*selected)[*index] = true;
  }
  return true;
}

bool IsValidDescriptors(
    const std::vector<mojom::BackupRecordDescriptorPtr>& records,
    const std::array<bool, 8>* selected) {
  if (records.size() > mojom::kMaxBackupRecords) {
    return false;
  }
  uint64_t total = 0u;
  std::set<std::pair<size_t, std::string_view>> identities;
  for (const auto& record : records) {
    const std::optional<size_t> kind =
        record ? KindIndex(record->kind) : std::nullopt;
    if (!record || !kind || (selected && !(*selected)[*kind]) ||
        !IsBoundedText(record->stable_id, mojom::kMaxBackupIdBytes) ||
        record->revision == 0u || record->schema_version == 0u ||
        record->plaintext_bytes > mojom::kMaxBackupRecordBytes ||
        !identities.emplace(*kind, record->stable_id).second) {
      return false;
    }
    const bool zero_digest = IsAllZero(record->plaintext_sha256);
    if (!IsKnownRecordState(record->state) ||
        (record->state == mojom::BackupRecordState::kActive &&
         (record->plaintext_bytes == 0u || zero_digest)) ||
        (record->state == mojom::BackupRecordState::kTombstone &&
         (record->plaintext_bytes != 0u || !zero_digest)) ||
        record->plaintext_bytes > mojom::kMaxBackupPlaintextBytes - total) {
      return false;
    }
    total += record->plaintext_bytes;
  }
  return true;
}

bool IsValidStagedRecords(
    const std::vector<mojom::StagedBackupRecordPtr>& records) {
  if (records.size() > mojom::kMaxBackupRecords) {
    return false;
  }
  uint64_t total = 0u;
  for (const auto& record : records) {
    const bool zero_digest = record && IsAllZero(record->plaintext_sha256);
    if (!record || record->plaintext_bytes > mojom::kMaxBackupRecordBytes ||
        (record->plaintext_bytes == 0u && !zero_digest) ||
        (record->plaintext_bytes != 0u && zero_digest) ||
        record->plaintext_bytes > mojom::kMaxBackupPlaintextBytes - total) {
      return false;
    }
    total += record->plaintext_bytes;
  }
  return true;
}

bool IsEmptyPrepareResult(const mojom::BackupManifestPrepareResult& result) {
  return result.manifest_plaintext.empty() &&
         IsAllZero(result.snapshot_sha256) &&
         result.payload_plaintext_bytes == 0u && result.source_order.empty() &&
         result.expected_sealed_chunks == 0u;
}

bool IsEmptyInspectResult(const mojom::BackupManifestInspectResult& result) {
  return result.backup_id.empty() && result.source_installation_id.empty() &&
         result.created_at_utc.empty() && result.selection.empty() &&
         result.record_count == 0u && IsAllZero(result.snapshot_sha256) &&
         result.payload_plaintext_bytes == 0u && result.records.empty();
}

}  // namespace

uint64_t BackupPlanningNowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

bool IsLiveBackupOperation(const mojom::OperationEnvelope* operation,
                           uint64_t service_generation,
                           uint64_t now_monotonic_ms) {
  return operation && operation->service_generation == service_generation &&
         service_generation != 0u && operation->task_revision == 0u &&
         operation->deadline_monotonic_ms > now_monotonic_ms &&
         IsBoundedText(operation->operation_id, mojom::kMaxOperationIdBytes) &&
         IsBoundedText(operation->idempotency_key,
                       mojom::kMaxIdempotencyKeyBytes);
}

bool IsExactBackupOperation(const mojom::OperationEnvelope* expected,
                            const mojom::OperationEnvelope* received) {
  return expected && received &&
         expected->operation_id == received->operation_id &&
         expected->service_generation == received->service_generation &&
         expected->task_revision == received->task_revision &&
         expected->deadline_monotonic_ms == received->deadline_monotonic_ms &&
         expected->idempotency_key == received->idempotency_key;
}

bool IsValidBackupPrepareRequest(
    const mojom::BackupManifestPrepareRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  std::array<bool, 8> selected{};
  return IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         IsBoundedText(request.backup_id, mojom::kMaxBackupIdBytes) &&
         IsBoundedText(request.source_installation_id,
                       mojom::kMaxBackupIdBytes) &&
         IsBoundedText(request.created_at_utc,
                       mojom::kMaxBackupTimestampBytes) &&
         IsValidSelection(request.selection, &selected) &&
         IsValidDescriptors(request.records, &selected);
}

bool IsValidBackupInspectRequest(
    const mojom::BackupManifestInspectRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         !request.manifest_plaintext.empty() &&
         request.manifest_plaintext.size() <= mojom::kMaxBackupManifestBytes;
}

bool IsValidBackupRestoreRequest(const mojom::BackupRestorePlanRequest& request,
                                 uint64_t service_generation,
                                 uint64_t now_monotonic_ms) {
  return IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         !request.manifest_plaintext.empty() &&
         request.manifest_plaintext.size() <= mojom::kMaxBackupManifestBytes &&
         IsValidStagedRecords(request.staged_records) &&
         IsValidDescriptors(request.current_records, nullptr) &&
         request.target && IsKnownTarget(request.target->kind) &&
         IsBoundedText(request.target->profile_id, mojom::kMaxBackupIdBytes);
}

bool IsValidBackupPrepareResult(
    const mojom::OperationEnvelope& expected_operation,
    size_t expected_record_count,
    const mojom::BackupManifestPrepareResult& result) {
  if (!IsExactBackupOperation(&expected_operation, result.operation.get()) ||
      !IsKnownStatus(result.status)) {
    return false;
  }
  if (result.status != mojom::BackupPlanningStatus::kSucceeded) {
    return IsEmptyPrepareResult(result);
  }
  if (expected_record_count > mojom::kMaxBackupRecords ||
      result.manifest_plaintext.empty() ||
      result.manifest_plaintext.size() > mojom::kMaxBackupManifestBytes ||
      IsAllZero(result.snapshot_sha256) ||
      result.payload_plaintext_bytes > mojom::kMaxBackupPlaintextBytes ||
      result.source_order.size() != expected_record_count) {
    return false;
  }
  const uint64_t payload_chunks =
      result.payload_plaintext_bytes == 0u
          ? 0u
          : 1u + ((result.payload_plaintext_bytes - 1u) /
                  mojom::kBackupPayloadChunkBytes);
  if (result.expected_sealed_chunks != payload_chunks + 1u ||
      result.expected_sealed_chunks > mojom::kMaxBackupSealedChunks) {
    return false;
  }
  std::vector<bool> seen(expected_record_count);
  for (const uint32_t ordinal : result.source_order) {
    if (ordinal >= seen.size() || seen[ordinal]) {
      return false;
    }
    seen[ordinal] = true;
  }
  return true;
}

bool IsValidBackupInspectResult(
    const mojom::OperationEnvelope& expected_operation,
    const mojom::BackupManifestInspectResult& result) {
  if (!IsExactBackupOperation(&expected_operation, result.operation.get()) ||
      !IsKnownStatus(result.status)) {
    return false;
  }
  if (result.status != mojom::BackupPlanningStatus::kSucceeded) {
    return IsEmptyInspectResult(result);
  }
  std::array<bool, 8> selected{};
  if (!IsBoundedText(result.backup_id, mojom::kMaxBackupIdBytes) ||
      !IsBoundedText(result.source_installation_id, mojom::kMaxBackupIdBytes) ||
      !IsBoundedText(result.created_at_utc, mojom::kMaxBackupTimestampBytes) ||
      !IsValidSelection(result.selection, &selected) ||
      result.record_count != result.records.size() ||
      result.records.size() > mojom::kMaxBackupRecords ||
      IsAllZero(result.snapshot_sha256) ||
      result.payload_plaintext_bytes > mojom::kMaxBackupPlaintextBytes) {
    return false;
  }
  std::optional<size_t> previous_kind;
  for (const mojom::BackupRecordKind kind : result.selection) {
    const std::optional<size_t> index = KindIndex(kind);
    if (!index || (previous_kind && *previous_kind >= *index)) {
      return false;
    }
    previous_kind = index;
  }
  uint64_t total = 0u;
  for (const auto& record : result.records) {
    if (!record || record->plaintext_bytes > mojom::kMaxBackupRecordBytes ||
        (record->state == mojom::BackupRecordState::kActive &&
         record->plaintext_bytes == 0u) ||
        (record->state == mojom::BackupRecordState::kTombstone &&
         record->plaintext_bytes != 0u) ||
        record->plaintext_bytes > mojom::kMaxBackupPlaintextBytes - total) {
      return false;
    }
    total += record->plaintext_bytes;
  }
  return total == result.payload_plaintext_bytes;
}

}  // namespace taffy
