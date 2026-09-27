// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_recovery_codec.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"

namespace taffy {
namespace {

namespace wire = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;

bool IsIntentId(std::string_view id) {
  return !id.empty() && id.size() <= wire::kMaxBackupIdBytes &&
         base::IsStringUTF8(id) &&
         std::ranges::none_of(
             id, [](unsigned char c) { return c < 0x20u || c == 0x7fu; });
}

bool IsIntent(wire::BackupRestorePhysicalIntent intent) {
  switch (intent) {
    case wire::BackupRestorePhysicalIntent::kCommitCandidate:
    case wire::BackupRestorePhysicalIntent::kAcceptCandidate:
    case wire::BackupRestorePhysicalIntent::kDiscardCandidate:
      return true;
  }
  return false;
}

bool IsOutcome(wire::BackupRestoreObservedOutcome outcome) {
  switch (outcome) {
    case wire::BackupRestoreObservedOutcome::kCompleted:
    case wire::BackupRestoreObservedOutcome::kDefinitelyNotCompleted:
    case wire::BackupRestoreObservedOutcome::kOutcomeUnknown:
      return true;
  }
  return false;
}

wire::BackupRestoreRecoveryRecordPtr DecodeRecord(
    const base::DictValue& value) {
  const auto version = value.FindInt("format_version");
  const auto kind = value.FindInt("fact_kind");
  const auto parsed_kind =
      kind && *kind >= 0
          ? core_service::wire::BackupRestoreRecoveryFactKindFromWire(*kind)
          : std::nullopt;
  const std::string* sequence_text = value.FindString("sequence");
  const base::DictValue* binding_value = value.FindDict("binding");
  uint64_t sequence = 0u;
  if (value.size() != 5u || !version || *version != 1 || !parsed_kind ||
      !sequence_text || !binding_value ||
      !base::StringToUint64(*sequence_text, &sequence) ||
      base::NumberToString(sequence) != *sequence_text || sequence == 0u ||
      sequence > wire::kMaxBackupRestoreRecoveryRecords) {
    return nullptr;
  }
  auto record = wire::BackupRestoreRecoveryRecord::New();
  record->format_version = 1u;
  record->sequence = sequence;
  record->binding =
      backup_restore_recovery_codec::DecodeBinding(*binding_value);
  record->fact_kind = *parsed_kind;
  if (!record->binding) {
    return nullptr;
  }
  if (record->fact_kind ==
      wire::BackupRestoreRecoveryFactKind::kIntentRecorded) {
    const base::DictValue* fact = value.FindDict("intent");
    if (!fact || fact->size() != 2u || !fact->FindString("intent_id") ||
        !fact->FindInt("intent")) {
      return nullptr;
    }
    const int encoded = *fact->FindInt("intent");
    const auto intent =
        encoded >= 0
            ? core_service::wire::BackupRestorePhysicalIntentFromWire(encoded)
            : std::nullopt;
    if (!intent) {
      return nullptr;
    }
    record->intent = wire::BackupRestoreRecoveryIntentFact::New(
        *fact->FindString("intent_id"), *intent);
  } else if (record->fact_kind ==
             wire::BackupRestoreRecoveryFactKind::kOutcomeObserved) {
    const base::DictValue* fact = value.FindDict("outcome");
    if (!fact || fact->size() != 2u || !fact->FindString("intent_id") ||
        !fact->FindInt("outcome")) {
      return nullptr;
    }
    const int encoded = *fact->FindInt("outcome");
    const auto outcome =
        encoded >= 0
            ? core_service::wire::BackupRestoreObservedOutcomeFromWire(encoded)
            : std::nullopt;
    if (!outcome) {
      return nullptr;
    }
    record->outcome = wire::BackupRestoreRecoveryOutcomeFact::New(
        *fact->FindString("intent_id"), *outcome);
  } else {
    return nullptr;
  }
  auto encoded = EncodeBackupRestoreRecoveryRecord(*record);
  return encoded && *encoded == value ? std::move(record) : nullptr;
}

}  // namespace

base::expected<base::DictValue, Error> EncodeBackupRestoreRecoveryRecord(
    const wire::BackupRestoreRecoveryRecord& record) {
  if (record.format_version != 1u || record.sequence == 0u ||
      record.sequence > wire::kMaxBackupRestoreRecoveryRecords ||
      !record.binding) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto binding = backup_restore_recovery_codec::EncodeBinding(*record.binding);
  if (!binding) {
    return base::unexpected(binding.error());
  }
  base::DictValue value;
  value.Set("format_version", 1);
  value.Set("sequence", base::NumberToString(record.sequence));
  value.Set("binding", std::move(*binding));
  value.Set("fact_kind", static_cast<int>(record.fact_kind));
  base::DictValue fact;
  switch (record.fact_kind) {
    case wire::BackupRestoreRecoveryFactKind::kIntentRecorded:
      if (!record.intent || record.outcome ||
          !IsIntentId(record.intent->intent_id) ||
          !IsIntent(record.intent->intent)) {
        return base::unexpected(Error::kInvalidArgument);
      }
      fact.Set("intent_id", record.intent->intent_id);
      fact.Set("intent", static_cast<int>(record.intent->intent));
      value.Set("intent", std::move(fact));
      break;
    case wire::BackupRestoreRecoveryFactKind::kOutcomeObserved:
      if (!record.outcome || record.intent ||
          !IsIntentId(record.outcome->intent_id) ||
          !IsOutcome(record.outcome->outcome)) {
        return base::unexpected(Error::kInvalidArgument);
      }
      fact.Set("intent_id", record.outcome->intent_id);
      fact.Set("outcome", static_cast<int>(record.outcome->outcome));
      value.Set("outcome", std::move(fact));
      break;
    default:
      return base::unexpected(Error::kInvalidArgument);
  }
  return value;
}

base::expected<BackupRestoreRecoveryRecords, Error>
DecodeBackupRestoreRecoveryJournal(const base::Value& value,
                                   std::string_view reservation_id,
                                   std::string_view target_profile_id) {
  if (!value.is_list() || value.GetList().empty() ||
      value.GetList().size() > wire::kMaxBackupRestoreRecoveryRecords) {
    return base::unexpected(Error::kCorrupt);
  }
  BackupRestoreRecoveryRecords records;
  uint64_t sequence = 1u;
  const base::DictValue* first_binding = nullptr;
  for (const auto& row : value.GetList()) {
    auto record = row.is_dict() ? DecodeRecord(row.GetDict()) : nullptr;
    if (!record || record->sequence != sequence ||
        record->binding->reservation_id != reservation_id ||
        record->binding->target_profile_id != target_profile_id) {
      return base::unexpected(Error::kCorrupt);
    }
    const base::DictValue* binding = row.GetDict().FindDict("binding");
    if (first_binding && *first_binding != *binding) {
      return base::unexpected(Error::kCorrupt);
    }
    first_binding = binding;
    records.push_back(std::move(record));
    ++sequence;
  }
  return records;
}

}  // namespace taffy
