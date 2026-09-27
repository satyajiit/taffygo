// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string_view>
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

bool IsValidIntentId(std::string_view value) {
  return !value.empty() && value.size() <= core_mojom::kMaxBackupIdBytes &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

// The bridge records land in `rust::Vec` fields, which no `std::vector`
// assigns into, so the rows are collected in the bridge's own shape here
// rather than converted a second time at each call site.
std::optional<rust::Vec<bridge::BridgeBackupRestoreRecoveryRecord>> ToRecords(
    const std::vector<core_mojom::BackupRestoreRecoveryRecordPtr>& input,
    size_t maximum) {
  if (input.size() > maximum) {
    return std::nullopt;
  }
  rust::Vec<bridge::BridgeBackupRestoreRecoveryRecord> output;
  output.reserve(input.size());
  for (const auto& record : input) {
    if (!record) {
      return std::nullopt;
    }
    auto projected = ToBridgeBackupRestoreRecoveryRecord(*record);
    if (!projected) {
      return std::nullopt;
    }
    output.push_back(std::move(*projected));
  }
  return output;
}

std::optional<bridge::BridgeBackupRestoreRecoveryResolutionAuthorization>
ToAuthorization(
    const core_mojom::BackupRestoreRecoveryResolutionAuthorization& input) {
  if (!input.binding || !input.decision_operation ||
      !wire::BackupRestoreResolutionChoiceFromWire(
          static_cast<uint32_t>(input.choice)) ||
      !IsValidIntentId(input.intent_id)) {
    return std::nullopt;
  }
  auto binding = ToBridgeBackupRestoreRecoveryBinding(*input.binding);
  auto operation = ToBridgeBackupOperation(*input.decision_operation);
  auto prefix = ToRecords(input.history_prefix,
                          core_mojom::kMaxBackupRestoreRecoveryRecords -
                              kRecoveryResolutionSuffixCapacity);
  if (!binding || !operation || !prefix) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreRecoveryResolutionAuthorization output;
  output.binding = std::move(*binding);
  output.decision_operation = std::move(*operation);
  output.choice = static_cast<uint8_t>(input.choice);
  output.intent_id = input.intent_id;
  output.history_prefix = std::move(*prefix);
  return output;
}

}  // namespace

std::optional<bridge::BridgeBackupRestoreRecoveryResolutionRequest>
ToBridgeBackupRestoreRecoveryResolutionRequest(
    const core_mojom::BackupRestoreRecoveryResolutionRequest& input) {
  if (!input.operation ||
      !wire::BackupRestoreResolutionChoiceFromWire(
          static_cast<uint32_t>(input.choice)) ||
      !IsValidIntentId(input.intent_id)) {
    return std::nullopt;
  }
  auto operation = ToBridgeBackupOperation(*input.operation);
  auto prefix = ToRecords(input.history_prefix,
                          core_mojom::kMaxBackupRestoreRecoveryRecords -
                              kRecoveryResolutionSuffixCapacity);
  if (!operation || !prefix) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreRecoveryResolutionRequest output;
  output.operation = std::move(*operation);
  output.history_prefix = std::move(*prefix);
  output.choice = static_cast<uint8_t>(input.choice);
  output.intent_id = input.intent_id;
  return output;
}

std::optional<bridge::BridgeBackupRestoreRecoveryResolutionOutcomeReport>
ToBridgeBackupRestoreRecoveryResolutionOutcomeReport(
    const core_mojom::BackupRestoreRecoveryResolutionOutcomeReport& input) {
  if (!input.operation || !input.authorization) {
    return std::nullopt;
  }
  auto operation = ToBridgeBackupOperation(*input.operation);
  auto authorization = ToAuthorization(*input.authorization);
  auto history = ToRecords(input.durable_history,
                           core_mojom::kMaxBackupRestoreRecoveryRecords);
  if (!operation || !authorization || !history) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreRecoveryResolutionOutcomeReport output;
  output.operation = std::move(*operation);
  output.authorization = std::move(*authorization);
  output.durable_history = std::move(*history);
  return output;
}

}  // namespace taffy::core_service_internal
