// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol_validation.h"

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

bool IsKnownRecordState(mojom::BackupRecordState state) {
  switch (state) {
    case mojom::BackupRecordState::kActive:
    case mojom::BackupRecordState::kTombstone:
      return true;
  }
  return false;
}

bool IsKnownAction(mojom::BackupRestoreAction action) {
  switch (action) {
    case mojom::BackupRestoreAction::kStageCreate:
    case mojom::BackupRestoreAction::kStageDeletion:
    case mojom::BackupRestoreAction::kAlreadyPresent:
    case mojom::BackupRestoreAction::kKeepNewerCurrent:
    case mojom::BackupRestoreAction::kBlockedByDeletion:
    case mojom::BackupRestoreAction::kNeedsExplicitConflictChoice:
      return true;
  }
  return false;
}

bool IsConflict(mojom::BackupRestoreAction action) {
  switch (action) {
    case mojom::BackupRestoreAction::kStageCreate:
    case mojom::BackupRestoreAction::kStageDeletion:
    case mojom::BackupRestoreAction::kAlreadyPresent:
    case mojom::BackupRestoreAction::kKeepNewerCurrent:
      return false;
    case mojom::BackupRestoreAction::kBlockedByDeletion:
    case mojom::BackupRestoreAction::kNeedsExplicitConflictChoice:
      return true;
  }
  return false;
}

bool IsEmptyRestoreResult(const mojom::BackupRestorePlanResult& result) {
  return result.backup_id.empty() && IsAllZero(result.snapshot_sha256) &&
         result.target && result.target->profile_id.empty() &&
         result.entries.empty() && !result.has_conflicts &&
         IsAllZero(result.confirmation_sha256) && !result.binding;
}

}  // namespace

bool IsValidBackupRestoreResult(
    const mojom::OperationEnvelope& expected_operation,
    const std::string& expected_owner_profile_id,
    const mojom::BackupRestoreTarget& expected_target,
    const std::vector<mojom::StagedBackupRecordPtr>& expected_staged_records,
    const mojom::BackupRestorePlanResult& result) {
  if (!IsExactBackupOperation(&expected_operation, result.operation.get()) ||
      !IsKnownStatus(result.status) || !result.target ||
      result.target->kind != expected_target.kind) {
    return false;
  }
  if (result.status != mojom::BackupPlanningStatus::kSucceeded) {
    return IsEmptyRestoreResult(result);
  }
  if (!IsBoundedText(result.backup_id, mojom::kMaxBackupIdBytes) ||
      IsAllZero(result.snapshot_sha256) ||
      result.target->profile_id != expected_target.profile_id ||
      result.entries.size() != expected_staged_records.size() ||
      result.entries.size() > mojom::kMaxBackupRecords ||
      IsAllZero(result.confirmation_sha256) || !result.binding ||
      !IsValidBackupRestoreBinding(result.binding.get(),
                                   expected_operation.service_generation) ||
      !IsExactBackupOperation(&expected_operation,
                              result.binding->planning_operation.get()) ||
      result.binding->owner_profile_id != expected_owner_profile_id ||
      !result.binding->target ||
      result.binding->target->kind != result.target->kind ||
      result.binding->target->profile_id != result.target->profile_id ||
      result.binding->backup_id != result.backup_id ||
      result.binding->snapshot_sha256 != result.snapshot_sha256 ||
      result.binding->confirmation_sha256 != result.confirmation_sha256) {
    return false;
  }
  std::optional<std::pair<size_t, std::string>> previous;
  bool found_conflict = false;
  for (size_t index = 0u; index < result.entries.size(); ++index) {
    const auto& entry = result.entries[index];
    const auto& staged = expected_staged_records[index];
    const std::optional<size_t> kind =
        entry ? KindIndex(entry->kind) : std::nullopt;
    const bool zero_digest = entry && IsAllZero(entry->plaintext_sha256);
    if (!entry || !kind || !IsKnownAction(entry->action) ||
        !IsKnownRecordState(entry->state) ||
        !IsBoundedText(entry->stable_id, mojom::kMaxBackupIdBytes) ||
        entry->archive_revision == 0u || entry->schema_version == 0u ||
        entry->plaintext_bytes > mojom::kMaxBackupRecordBytes ||
        (entry->state == mojom::BackupRecordState::kActive &&
         (entry->plaintext_bytes == 0u || zero_digest)) ||
        (entry->state == mojom::BackupRecordState::kTombstone &&
         (entry->plaintext_bytes != 0u || !zero_digest)) ||
        !staged || entry->plaintext_bytes != staged->plaintext_bytes ||
        entry->plaintext_sha256 != staged->plaintext_sha256) {
      return false;
    }
    std::pair<size_t, std::string> identity(*kind, entry->stable_id);
    if (previous && *previous >= identity) {
      return false;
    }
    previous = std::move(identity);
    found_conflict |= IsConflict(entry->action);
    if (expected_target.kind ==
            mojom::BackupRestoreTargetKind::kNewRegularProfile &&
        entry->action != mojom::BackupRestoreAction::kStageCreate &&
        entry->action != mojom::BackupRestoreAction::kStageDeletion) {
      return false;
    }
    if ((entry->action == mojom::BackupRestoreAction::kStageCreate) !=
            (entry->state == mojom::BackupRecordState::kActive) &&
        (entry->action == mojom::BackupRestoreAction::kStageCreate ||
         entry->action == mojom::BackupRestoreAction::kStageDeletion)) {
      return false;
    }
  }
  return result.has_conflicts == found_conflict;
}

}  // namespace taffy
