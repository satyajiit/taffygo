// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_recovery_presentation.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol_validation.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"

namespace taffy::backup_restore_recovery_presentation_internal {
namespace {

namespace mojom = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;

constexpr size_t kMaximumTargetLabelCodeUnits = 40u;
constexpr size_t kActionCount = 6u;
constexpr char kTargetProfileLabelKey[] = "target_profile_label";
constexpr char kSourceProfileIdKey[] = "source_profile_id";
constexpr char kTargetProfileIdKey[] = "target_profile_id";
constexpr char kBackupIdKey[] = "backup_id";
constexpr char kSnapshotSha256Key[] = "snapshot_sha256";
constexpr char kConfirmationSha256Key[] = "confirmation_sha256";
constexpr char kSelectedClassesKey[] = "selected_classes";
constexpr char kHasConflictsKey[] = "has_conflicts";
constexpr char kCanStageKey[] = "can_stage";
constexpr char kKindKey[] = "kind";
constexpr char kActionCountsKey[] = "action_counts";

bool IsBoundedText(std::string_view value) {
  return !value.empty() && value.size() <= mojom::kMaxBackupIdBytes &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

bool IsValidLabel(std::u16string_view value) {
  if (value.empty() || value.size() > kMaximumTargetLabelCodeUnits) {
    return false;
  }
  const std::string encoded = base::UTF16ToUTF8(value);
  return base::UTF8ToUTF16(encoded) == value &&
         base::TrimWhitespace(value, base::TRIM_ALL) == value &&
         std::ranges::none_of(value, [](char16_t character) {
           return character < 0x20u || character == 0x7fu;
         });
}

template <typename Range>
bool IsDigest(const Range& digest) {
  return digest.size() == 32u &&
         std::ranges::any_of(digest, [](uint8_t byte) { return byte != 0u; });
}

std::optional<size_t> ActionIndex(mojom::BackupRestoreAction action) {
  const uint32_t encoded = static_cast<uint32_t>(action);
  if (encoded >= kActionCount ||
      !core_service::wire::BackupRestoreActionFromWire(encoded)) {
    return std::nullopt;
  }
  return static_cast<size_t>(encoded);
}

bool IsKnownKind(mojom::BackupRecordKind kind) {
  switch (kind) {
    case mojom::BackupRecordKind::kAssistantConfiguration:
    case mojom::BackupRecordKind::kSavedWorkspace:
    case mojom::BackupRecordKind::kLibraryEntry:
    case mojom::BackupRecordKind::kMemoryRecord:
    case mojom::BackupRecordKind::kUserAuthoredSkill:
    case mojom::BackupRecordKind::kLearnedProcedure:
      return true;
    case mojom::BackupRecordKind::kBookmark:
    case mojom::BackupRecordKind::kBrowserPreference:
      return false;
  }
  return false;
}

bool IsValidClasses(
    const std::vector<BackupRestoreRecoveryPresentationClass>& classes,
    uint64_t* total_count,
    bool* counts_have_conflicts,
    bool* counts_can_stage) {
  if (!total_count || !counts_have_conflicts || !counts_can_stage ||
      classes.empty() || classes.size() > 6u) {
    return false;
  }
  *total_count = 0u;
  *counts_have_conflicts = false;
  *counts_can_stage = true;
  std::optional<uint32_t> previous_kind;
  for (const auto& row : classes) {
    const uint32_t kind = static_cast<uint32_t>(row.kind);
    if (!IsKnownKind(row.kind) || (previous_kind && kind <= *previous_kind)) {
      return false;
    }
    previous_kind = kind;
    for (size_t action = 0u; action < row.action_counts.size(); ++action) {
      const uint32_t count = row.action_counts[action];
      if (count > mojom::kMaxBackupRecords ||
          count > mojom::kMaxBackupRecords - *total_count) {
        return false;
      }
      *total_count += count;
      if (count != 0u && action >= 2u) {
        *counts_can_stage = false;
      }
      if (count != 0u && action >= 4u) {
        *counts_have_conflicts = true;
      }
    }
  }
  return true;
}

bool IsValidPresentation(
    const BackupRestoreRecoveryPresentation& presentation) {
  uint64_t total_count = 0u;
  bool counts_have_conflicts = false;
  bool counts_can_stage = false;
  return IsValidLabel(presentation.target_profile_label) &&
         IsBoundedText(presentation.source_profile_id) &&
         IsBoundedText(presentation.target_profile_id) &&
         presentation.source_profile_id != presentation.target_profile_id &&
         IsBoundedText(presentation.backup_id) &&
         IsDigest(presentation.snapshot_sha256) &&
         IsDigest(presentation.confirmation_sha256) &&
         IsValidClasses(presentation.selected_classes, &total_count,
                        &counts_have_conflicts, &counts_can_stage) &&
         presentation.has_conflicts == counts_have_conflicts &&
         presentation.can_stage ==
             (counts_can_stage && !presentation.has_conflicts);
}

std::array<uint8_t, 32> DecodeDigest(const base::DictValue& value,
                                     std::string_view key) {
  const std::string* encoded = value.FindString(key);
  std::vector<uint8_t> bytes;
  std::array<uint8_t, 32> digest{};
  if (!encoded || encoded->size() != 64u ||
      !base::HexStringToBytes(*encoded, &bytes) ||
      bytes.size() != digest.size() ||
      base::HexEncodeLower(bytes) != *encoded || !IsDigest(bytes)) {
    return {};
  }
  std::ranges::copy(bytes, digest.begin());
  return digest;
}

}  // namespace

base::expected<BackupRestoreRecoveryPresentation, Error> Build(
    std::u16string target_profile_label,
    base::span<const mojom::BackupRecordKind> original_selection,
    const mojom::BackupRestorePlanResult& exact_plan) {
  if (!IsValidLabel(target_profile_label) ||
      !storage::backup::IsSupportedBackupStorageSelection(original_selection) ||
      exact_plan.status != mojom::BackupPlanningStatus::kSucceeded ||
      !exact_plan.operation || !exact_plan.target || !exact_plan.binding ||
      !exact_plan.binding->planning_operation ||
      !IsValidBackupRestoreBinding(
          exact_plan.binding.get(),
          exact_plan.binding->planning_operation->service_generation) ||
      !IsExactBackupOperation(exact_plan.operation.get(),
                              exact_plan.binding->planning_operation.get()) ||
      exact_plan.target->kind !=
          mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      exact_plan.binding->target->kind != exact_plan.target->kind ||
      exact_plan.binding->target->profile_id != exact_plan.target->profile_id ||
      exact_plan.binding->backup_id != exact_plan.backup_id ||
      exact_plan.binding->snapshot_sha256 != exact_plan.snapshot_sha256 ||
      exact_plan.binding->confirmation_sha256 !=
          exact_plan.confirmation_sha256 ||
      exact_plan.entries.size() > mojom::kMaxBackupRecords) {
    return base::unexpected(Error::kInvalidArgument);
  }

  BackupRestoreRecoveryPresentation presentation;
  presentation.target_profile_label = std::move(target_profile_label);
  presentation.source_profile_id = exact_plan.binding->owner_profile_id;
  presentation.target_profile_id = exact_plan.binding->target->profile_id;
  presentation.backup_id = exact_plan.binding->backup_id;
  std::ranges::copy(exact_plan.binding->snapshot_sha256,
                    presentation.snapshot_sha256.begin());
  std::ranges::copy(exact_plan.binding->confirmation_sha256,
                    presentation.confirmation_sha256.begin());
  presentation.selected_classes.reserve(original_selection.size());
  std::vector<mojom::BackupRecordKind> ordered_selection(
      original_selection.begin(), original_selection.end());
  std::ranges::sort(ordered_selection);
  for (mojom::BackupRecordKind kind : ordered_selection) {
    presentation.selected_classes.push_back({.kind = kind});
  }

  bool found_conflict = false;
  bool can_stage = true;
  for (const auto& entry : exact_plan.entries) {
    const std::optional<size_t> action =
        entry ? ActionIndex(entry->action) : std::nullopt;
    auto row =
        entry ? std::ranges::find(presentation.selected_classes, entry->kind,
                                  &BackupRestoreRecoveryPresentationClass::kind)
              : presentation.selected_classes.end();
    if (!entry || !action || row == presentation.selected_classes.end() ||
        row->action_counts[*action] ==
            static_cast<uint32_t>(mojom::kMaxBackupRecords)) {
      return base::unexpected(Error::kInvalidArgument);
    }
    ++row->action_counts[*action];
    found_conflict |= *action >= 4u;
    can_stage &= *action < 2u;
  }
  if (exact_plan.has_conflicts != found_conflict) {
    return base::unexpected(Error::kInvalidArgument);
  }
  presentation.has_conflicts = found_conflict;
  presentation.can_stage = can_stage && !found_conflict;
  return IsValidPresentation(presentation)
             ? base::expected<BackupRestoreRecoveryPresentation, Error>(
                   std::move(presentation))
             : base::unexpected(Error::kInvalidArgument);
}

base::expected<base::DictValue, Error> Encode(
    const BackupRestoreRecoveryPresentation& presentation) {
  if (!IsValidPresentation(presentation)) {
    return base::unexpected(Error::kInvalidArgument);
  }
  base::DictValue value;
  value.Set(kTargetProfileLabelKey,
            base::UTF16ToUTF8(presentation.target_profile_label));
  value.Set(kSourceProfileIdKey, presentation.source_profile_id);
  value.Set(kTargetProfileIdKey, presentation.target_profile_id);
  value.Set(kBackupIdKey, presentation.backup_id);
  value.Set(kSnapshotSha256Key,
            base::HexEncodeLower(presentation.snapshot_sha256));
  value.Set(kConfirmationSha256Key,
            base::HexEncodeLower(presentation.confirmation_sha256));
  base::ListValue selected_classes;
  for (const auto& row : presentation.selected_classes) {
    base::DictValue encoded_row;
    encoded_row.Set(kKindKey, static_cast<int>(row.kind));
    base::ListValue counts;
    for (uint32_t count : row.action_counts) {
      counts.Append(static_cast<int>(count));
    }
    encoded_row.Set(kActionCountsKey, std::move(counts));
    selected_classes.Append(std::move(encoded_row));
  }
  value.Set(kSelectedClassesKey, std::move(selected_classes));
  value.Set(kHasConflictsKey, presentation.has_conflicts);
  value.Set(kCanStageKey, presentation.can_stage);
  return value;
}

base::expected<BackupRestoreRecoveryPresentation, Error> Decode(
    const base::Value& value) {
  if (!value.is_dict() || value.GetDict().size() != 9u) {
    return base::unexpected(Error::kCorrupt);
  }
  const base::DictValue& encoded = value.GetDict();
  const std::string* label = encoded.FindString(kTargetProfileLabelKey);
  const std::string* source = encoded.FindString(kSourceProfileIdKey);
  const std::string* target = encoded.FindString(kTargetProfileIdKey);
  const std::string* backup = encoded.FindString(kBackupIdKey);
  const base::ListValue* selected = encoded.FindList(kSelectedClassesKey);
  const std::optional<bool> has_conflicts = encoded.FindBool(kHasConflictsKey);
  const std::optional<bool> can_stage = encoded.FindBool(kCanStageKey);
  if (!label || !base::IsStringUTF8(*label) || !source || !target || !backup ||
      !selected || !has_conflicts || !can_stage || selected->empty() ||
      selected->size() > 6u) {
    return base::unexpected(Error::kCorrupt);
  }

  BackupRestoreRecoveryPresentation presentation;
  presentation.target_profile_label = base::UTF8ToUTF16(*label);
  presentation.source_profile_id = *source;
  presentation.target_profile_id = *target;
  presentation.backup_id = *backup;
  presentation.snapshot_sha256 = DecodeDigest(encoded, kSnapshotSha256Key);
  presentation.confirmation_sha256 =
      DecodeDigest(encoded, kConfirmationSha256Key);
  presentation.has_conflicts = *has_conflicts;
  presentation.can_stage = *can_stage;
  presentation.selected_classes.reserve(selected->size());
  for (const base::Value& encoded_row : *selected) {
    if (!encoded_row.is_dict() || encoded_row.GetDict().size() != 2u) {
      return base::unexpected(Error::kCorrupt);
    }
    const std::optional<int> kind = encoded_row.GetDict().FindInt(kKindKey);
    const base::ListValue* counts =
        encoded_row.GetDict().FindList(kActionCountsKey);
    if (!kind || *kind < 0 || !counts || counts->size() != kActionCount) {
      return base::unexpected(Error::kCorrupt);
    }
    auto parsed_kind = core_service::wire::BackupRecordKindFromWire(
        static_cast<uint32_t>(*kind));
    if (!parsed_kind) {
      return base::unexpected(Error::kCorrupt);
    }
    BackupRestoreRecoveryPresentationClass row{.kind = *parsed_kind};
    for (size_t action = 0u; action < counts->size(); ++action) {
      const base::Value& count = (*counts)[action];
      if (!count.is_int() || count.GetInt() < 0 ||
          static_cast<uint64_t>(count.GetInt()) > mojom::kMaxBackupRecords) {
        return base::unexpected(Error::kCorrupt);
      }
      row.action_counts[action] = static_cast<uint32_t>(count.GetInt());
    }
    presentation.selected_classes.push_back(std::move(row));
  }
  return IsValidPresentation(presentation)
             ? base::expected<BackupRestoreRecoveryPresentation, Error>(
                   std::move(presentation))
             : base::unexpected(Error::kCorrupt);
}

bool MatchesRecoveryBinding(
    const BackupRestoreRecoveryPresentation& presentation,
    std::string_view reservation_id,
    const mojom::BackupRestoreRecoveryBinding& binding) {
  if (!IsValidPresentation(presentation) || reservation_id.empty()) {
    return false;
  }
  uint64_t presented_count = 0u;
  std::vector<mojom::BackupRecordKind> presented_nonempty_classes;
  for (const auto& row : presentation.selected_classes) {
    uint64_t class_count = 0u;
    for (uint32_t count : row.action_counts) {
      class_count += count;
    }
    presented_count += class_count;
    if (class_count != 0u) {
      presented_nonempty_classes.push_back(row.kind);
    }
  }
  return binding.reservation_id == reservation_id &&
         binding.owner_profile_id == presentation.source_profile_id &&
         binding.target_kind ==
             mojom::BackupRestoreTargetKind::kNewRegularProfile &&
         binding.target_profile_id == presentation.target_profile_id &&
         binding.backup_id == presentation.backup_id &&
         std::ranges::equal(binding.snapshot_sha256,
                            presentation.snapshot_sha256) &&
         std::ranges::equal(binding.confirmation_sha256,
                            presentation.confirmation_sha256) &&
         binding.record_count == presented_count &&
         binding.selection == presented_nonempty_classes;
}

}  // namespace taffy::backup_restore_recovery_presentation_internal
