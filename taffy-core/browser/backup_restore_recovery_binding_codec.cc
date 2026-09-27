// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "taffy/browser/backup_restore_recovery_codec.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"

namespace taffy::backup_restore_recovery_codec {
namespace {

namespace wire = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;

bool IsId(std::string_view id) {
  return !id.empty() && id.size() <= wire::kMaxBackupIdBytes &&
         base::IsStringUTF8(id) &&
         std::ranges::none_of(
             id, [](unsigned char c) { return c < 0x20u || c == 0x7fu; });
}

bool IsDigest(const std::vector<uint8_t>& digest) {
  return digest.size() == 32u &&
         std::ranges::any_of(digest, [](uint8_t b) { return b != 0u; });
}

bool IsRecordKind(int kind) {
  return kind >= 0 &&
         core_service::wire::BackupRecordKindFromWire(kind).has_value();
}

bool IsBinding(const wire::BackupRestoreRecoveryBinding& binding) {
  if (!IsId(binding.reservation_id) || !IsId(binding.owner_profile_id) ||
      !IsId(binding.target_profile_id) || !IsId(binding.backup_id) ||
      binding.owner_profile_id == binding.target_profile_id ||
      binding.target_kind !=
          wire::BackupRestoreTargetKind::kNewRegularProfile ||
      !IsDigest(binding.snapshot_sha256) ||
      !IsDigest(binding.confirmation_sha256) ||
      !IsDigest(binding.candidate_records_sha256) ||
      binding.record_count > wire::kMaxBackupRecords ||
      binding.selection.size() > 8u ||
      binding.selection.empty() != (binding.record_count == 0u) ||
      binding.selection.size() > binding.record_count) {
    return false;
  }
  int previous = -1;
  for (auto kind : binding.selection) {
    const int encoded = static_cast<int>(kind);
    if (!IsRecordKind(encoded) || encoded <= previous) {
      return false;
    }
    previous = encoded;
  }
  return true;
}

std::vector<uint8_t> Digest(const base::DictValue& value,
                            std::string_view key) {
  const std::string* encoded = value.FindString(key);
  std::vector<uint8_t> digest;
  if (!encoded || encoded->size() != 64u ||
      !base::HexStringToBytes(*encoded, &digest) ||
      base::HexEncodeLower(digest) != *encoded || !IsDigest(digest)) {
    return {};
  }
  return digest;
}

}  // namespace

base::expected<base::DictValue, Error> EncodeBinding(
    const wire::BackupRestoreRecoveryBinding& binding) {
  if (!IsBinding(binding)) {
    return base::unexpected(Error::kInvalidArgument);
  }
  base::DictValue value;
  value.Set("reservation_id", binding.reservation_id);
  value.Set("owner_profile_id", binding.owner_profile_id);
  value.Set("target_kind", static_cast<int>(binding.target_kind));
  value.Set("target_profile_id", binding.target_profile_id);
  value.Set("backup_id", binding.backup_id);
  value.Set("snapshot_sha256", base::HexEncodeLower(binding.snapshot_sha256));
  value.Set("confirmation_sha256",
            base::HexEncodeLower(binding.confirmation_sha256));
  value.Set("candidate_records_sha256",
            base::HexEncodeLower(binding.candidate_records_sha256));
  value.Set("record_count", base::NumberToString(binding.record_count));
  base::ListValue selection;
  for (auto kind : binding.selection) {
    selection.Append(static_cast<int>(kind));
  }
  value.Set("selection", std::move(selection));
  return value;
}

wire::BackupRestoreRecoveryBindingPtr DecodeBinding(
    const base::DictValue& value) {
  const std::string* reservation = value.FindString("reservation_id");
  const std::string* owner = value.FindString("owner_profile_id");
  const std::string* target = value.FindString("target_profile_id");
  const std::string* backup = value.FindString("backup_id");
  const auto target_kind = value.FindInt("target_kind");
  const auto parsed_kind =
      target_kind && *target_kind >= 0
          ? core_service::wire::BackupRestoreTargetKindFromWire(*target_kind)
          : std::nullopt;
  const std::string* count_text = value.FindString("record_count");
  const base::ListValue* selection = value.FindList("selection");
  uint64_t count = 0u;
  if (value.size() != 10u || !reservation || !owner || !target || !backup ||
      !parsed_kind || !count_text || !selection ||
      !base::StringToUint64(*count_text, &count) ||
      base::NumberToString(count) != *count_text) {
    return nullptr;
  }
  auto binding = wire::BackupRestoreRecoveryBinding::New();
  binding->reservation_id = *reservation;
  binding->owner_profile_id = *owner;
  binding->target_profile_id = *target;
  binding->target_kind = *parsed_kind;
  binding->backup_id = *backup;
  binding->snapshot_sha256 = Digest(value, "snapshot_sha256");
  binding->confirmation_sha256 = Digest(value, "confirmation_sha256");
  binding->candidate_records_sha256 = Digest(value, "candidate_records_sha256");
  binding->record_count = count;
  for (const auto& kind : *selection) {
    if (!kind.is_int() || !IsRecordKind(kind.GetInt())) {
      return nullptr;
    }
    binding->selection.push_back(
        *core_service::wire::BackupRecordKindFromWire(kind.GetInt()));
  }
  return IsBinding(*binding) ? std::move(binding) : nullptr;
}

}  // namespace taffy::backup_restore_recovery_codec
