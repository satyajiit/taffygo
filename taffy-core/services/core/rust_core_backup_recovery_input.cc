// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string_view>
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

bool IsBoundedText(std::string_view value) {
  return value.size() <= core_mojom::kMaxBackupIdBytes &&
         base::IsStringUTF8(value);
}

bool IsDigest(const std::vector<uint8_t>& value) {
  return value.size() == 32u;
}

std::optional<bridge::BridgeBackupRestoreRecoveryBinding> ToBinding(
    const core_mojom::BackupRestoreRecoveryBinding* input) {
  if (!input || !IsBoundedText(input->reservation_id) ||
      !IsBoundedText(input->owner_profile_id) ||
      !IsBoundedText(input->target_profile_id) ||
      !IsBoundedText(input->backup_id) ||
      !wire::BackupRestoreTargetKindFromWire(
          static_cast<uint32_t>(input->target_kind)) ||
      !IsDigest(input->snapshot_sha256) ||
      !IsDigest(input->confirmation_sha256) ||
      !IsDigest(input->candidate_records_sha256) ||
      input->selection.size() > core_mojom::kMaxBackupSelectionKinds) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreRecoveryBinding output;
  output.reservation_id = input->reservation_id;
  output.owner_profile_id = input->owner_profile_id;
  output.target_kind = static_cast<uint8_t>(input->target_kind);
  output.target_profile_id = input->target_profile_id;
  output.backup_id = input->backup_id;
  std::ranges::copy(input->snapshot_sha256, output.snapshot_sha256.begin());
  std::ranges::copy(input->confirmation_sha256,
                    output.confirmation_sha256.begin());
  output.selection.reserve(input->selection.size());
  for (const auto kind : input->selection) {
    if (!wire::BackupRecordKindFromWire(static_cast<uint32_t>(kind))) {
      return std::nullopt;
    }
    output.selection.push_back(static_cast<uint8_t>(kind));
  }
  output.record_count = input->record_count;
  std::ranges::copy(input->candidate_records_sha256,
                    output.candidate_records_sha256.begin());
  return output;
}

std::optional<bridge::BridgeBackupRestoreRecoveryRecord> ToRecord(
    const core_mojom::BackupRestoreRecoveryRecord* input) {
  if (!input || !input->binding ||
      !wire::BackupRestoreRecoveryFactKindFromWire(
          static_cast<uint32_t>(input->fact_kind))) {
    return std::nullopt;
  }
  auto binding = ToBinding(input->binding.get());
  if (!binding) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreRecoveryRecord output;
  output.format_version = input->format_version;
  output.sequence = input->sequence;
  output.binding = std::move(*binding);
  output.fact_kind = static_cast<uint8_t>(input->fact_kind);
  output.has_intent = !!input->intent;
  if (input->intent) {
    if (!IsBoundedText(input->intent->intent_id) ||
        !wire::BackupRestorePhysicalIntentFromWire(
            static_cast<uint32_t>(input->intent->intent))) {
      return std::nullopt;
    }
    output.intent.intent_id = input->intent->intent_id;
    output.intent.intent = static_cast<uint8_t>(input->intent->intent);
  }
  output.has_outcome = !!input->outcome;
  if (input->outcome) {
    if (!IsBoundedText(input->outcome->intent_id) ||
        !wire::BackupRestoreObservedOutcomeFromWire(
            static_cast<uint32_t>(input->outcome->outcome))) {
      return std::nullopt;
    }
    output.outcome.intent_id = input->outcome->intent_id;
    output.outcome.outcome = static_cast<uint8_t>(input->outcome->outcome);
  }
  return output;
}

}  // namespace

std::optional<bridge::BridgeBackupRestoreRecoveryBinding>
ToBridgeBackupRestoreRecoveryBinding(
    const core_mojom::BackupRestoreRecoveryBinding& input) {
  return ToBinding(&input);
}

std::optional<bridge::BridgeBackupRestoreRecoveryRecord>
ToBridgeBackupRestoreRecoveryRecord(
    const core_mojom::BackupRestoreRecoveryRecord& input) {
  return ToRecord(&input);
}

std::optional<bridge::BridgeBackupRestoreRecoveryInspectionRequest>
ToBridgeBackupRestoreRecoveryInspectionRequest(
    const core_mojom::BackupRestoreRecoveryInspectionRequest& input) {
  if (!input.operation ||
      input.records.size() > core_mojom::kMaxBackupRestoreRecoveryRecords) {
    return std::nullopt;
  }
  auto operation = ToBridgeBackupOperation(*input.operation);
  if (!operation) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreRecoveryInspectionRequest output;
  output.operation = std::move(*operation);
  output.records.reserve(input.records.size());
  for (const auto& record : input.records) {
    auto projected = ToRecord(record.get());
    if (!projected) {
      return std::nullopt;
    }
    output.records.push_back(std::move(*projected));
  }
  return output;
}

}  // namespace taffy::core_service_internal
