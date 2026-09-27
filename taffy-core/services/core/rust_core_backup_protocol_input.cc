// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string_view>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_backup.h"
#include "taffy/services/core/rust_core_skill_records.h"

namespace taffy::core_service_internal {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;
namespace wire = core_service::wire;

bool IsBoundedText(std::string_view value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

template <typename Range>
bool IsNonZeroDigest(const Range& value) {
  return value.size() == 32u &&
         std::ranges::any_of(value, [](uint8_t byte) { return byte != 0u; });
}

std::optional<bridge::BridgeBackupRestoreBinding> ToBinding(
    const mojom::BackupRestoreBinding* input) {
  if (!input || !input->planning_operation || !input->target ||
      input->target->kind !=
          mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      !IsBoundedText(input->owner_profile_id, mojom::kMaxBackupIdBytes) ||
      !IsBoundedText(input->target->profile_id, mojom::kMaxBackupIdBytes) ||
      input->owner_profile_id == input->target->profile_id ||
      !IsBoundedText(input->backup_id, mojom::kMaxBackupIdBytes) ||
      !IsNonZeroDigest(input->snapshot_sha256) ||
      !IsNonZeroDigest(input->confirmation_sha256)) {
    return std::nullopt;
  }
  std::optional<bridge::BridgeBackupOperation> operation =
      ToBridgeBackupOperation(*input->planning_operation);
  if (!operation) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreBinding output;
  output.planning_operation = std::move(*operation);
  output.owner_profile_id = input->owner_profile_id;
  output.target_kind = static_cast<uint8_t>(input->target->kind);
  output.target_profile_id = input->target->profile_id;
  output.backup_id = input->backup_id;
  std::ranges::copy(input->snapshot_sha256, output.snapshot_sha256.begin());
  std::ranges::copy(input->confirmation_sha256,
                    output.confirmation_sha256.begin());
  return output;
}

std::optional<bridge::BridgeBackupRestoreStageAuthorization> ToStage(
    const mojom::BackupRestoreStageAuthorization* input) {
  if (!input || !input->binding || !input->decision_operation) {
    return std::nullopt;
  }
  std::optional<bridge::BridgeBackupRestoreBinding> binding =
      ToBinding(input->binding.get());
  std::optional<bridge::BridgeBackupOperation> operation =
      ToBridgeBackupOperation(*input->decision_operation);
  if (!binding || !operation) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreStageAuthorization output;
  output.binding = std::move(*binding);
  output.decision_operation = std::move(*operation);
  return output;
}

std::optional<bridge::BridgeBackupRestoreCommitAuthorization> ToCommit(
    const mojom::BackupRestoreCommitAuthorization* input) {
  if (!input || !input->binding || !input->decision_operation) {
    return std::nullopt;
  }
  std::optional<bridge::BridgeBackupRestoreBinding> binding =
      ToBinding(input->binding.get());
  std::optional<bridge::BridgeBackupOperation> operation =
      ToBridgeBackupOperation(*input->decision_operation);
  if (!binding || !operation) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreCommitAuthorization output;
  output.binding = std::move(*binding);
  output.decision_operation = std::move(*operation);
  return output;
}

std::optional<bridge::BridgeBackupRestoreResolutionAuthorization> ToResolution(
    const mojom::BackupRestoreResolutionAuthorization* input) {
  if (!input || !input->binding || !input->decision_operation ||
      !wire::BackupRestoreResolutionChoiceFromWire(
          static_cast<uint32_t>(input->choice))) {
    return std::nullopt;
  }
  std::optional<bridge::BridgeBackupRestoreBinding> binding =
      ToBinding(input->binding.get());
  std::optional<bridge::BridgeBackupOperation> operation =
      ToBridgeBackupOperation(*input->decision_operation);
  if (!binding || !operation) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreResolutionAuthorization output;
  output.binding = std::move(*binding);
  output.decision_operation = std::move(*operation);
  output.choice = static_cast<uint8_t>(input->choice);
  return output;
}

}  // namespace

std::optional<bridge::BridgeBackupRestorePlanConfirmationRequest>
ToBridgeBackupRestorePlanConfirmationRequest(
    const mojom::BackupRestorePlanConfirmationRequest& input) {
  std::optional<bridge::BridgeBackupOperation> operation =
      input.operation ? ToBridgeBackupOperation(*input.operation)
                      : std::nullopt;
  std::optional<bridge::BridgeBackupRestoreBinding> binding =
      ToBinding(input.binding.get());
  if (!operation || !binding || !IsNonZeroDigest(input.confirmed_sha256)) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestorePlanConfirmationRequest output;
  output.operation = std::move(*operation);
  output.binding = std::move(*binding);
  std::ranges::copy(input.confirmed_sha256, output.confirmed_sha256.begin());
  return output;
}

std::optional<bridge::BridgeBackupRestoreStageVerificationRequest>
ToBridgeBackupRestoreStageVerificationRequest(
    const mojom::BackupRestoreStageVerificationRequest& input) {
  std::optional<bridge::BridgeBackupOperation> operation =
      input.operation ? ToBridgeBackupOperation(*input.operation)
                      : std::nullopt;
  std::optional<bridge::BridgeBackupRestoreStageAuthorization> authorization =
      ToStage(input.authorization.get());
  auto skills = ToBridgeSkillRecords(input.skills);
  if (!operation || !authorization ||
      !IsNonZeroDigest(input.staged_snapshot_sha256) || !skills) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreStageVerificationRequest output;
  output.operation = std::move(*operation);
  output.authorization = std::move(*authorization);
  output.skills = std::move(*skills);
  std::ranges::copy(input.staged_snapshot_sha256,
                    output.staged_snapshot_sha256.begin());
  return output;
}

std::optional<bridge::BridgeBackupRestoreCommitOutcomeReport>
ToBridgeBackupRestoreCommitOutcomeReport(
    const mojom::BackupRestoreCommitOutcomeReport& input) {
  std::optional<bridge::BridgeBackupOperation> operation =
      input.operation ? ToBridgeBackupOperation(*input.operation)
                      : std::nullopt;
  std::optional<bridge::BridgeBackupRestoreCommitAuthorization> authorization =
      ToCommit(input.authorization.get());
  if (!operation || !authorization ||
      !wire::BackupRestoreCommitOutcomeFromWire(
          static_cast<uint32_t>(input.outcome))) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreCommitOutcomeReport output;
  output.operation = std::move(*operation);
  output.authorization = std::move(*authorization);
  output.outcome = static_cast<uint8_t>(input.outcome);
  return output;
}

std::optional<bridge::BridgeBackupRestoreResolutionRequest>
ToBridgeBackupRestoreResolutionRequest(
    const mojom::BackupRestoreResolutionRequest& input) {
  std::optional<bridge::BridgeBackupOperation> operation =
      input.operation ? ToBridgeBackupOperation(*input.operation)
                      : std::nullopt;
  std::optional<bridge::BridgeBackupRestoreBinding> binding =
      ToBinding(input.binding.get());
  if (!operation || !binding ||
      !wire::BackupRestoreResolutionChoiceFromWire(
          static_cast<uint32_t>(input.choice))) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreResolutionRequest output;
  output.operation = std::move(*operation);
  output.binding = std::move(*binding);
  output.choice = static_cast<uint8_t>(input.choice);
  return output;
}

std::optional<bridge::BridgeBackupRestoreResolutionOutcomeReport>
ToBridgeBackupRestoreResolutionOutcomeReport(
    const mojom::BackupRestoreResolutionOutcomeReport& input) {
  std::optional<bridge::BridgeBackupOperation> operation =
      input.operation ? ToBridgeBackupOperation(*input.operation)
                      : std::nullopt;
  std::optional<bridge::BridgeBackupRestoreResolutionAuthorization>
      authorization = ToResolution(input.authorization.get());
  if (!operation || !authorization ||
      !wire::BackupRestoreResolutionOutcomeFromWire(
          static_cast<uint32_t>(input.outcome))) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreResolutionOutcomeReport output;
  output.operation = std::move(*operation);
  output.authorization = std::move(*authorization);
  output.outcome = static_cast<uint8_t>(input.outcome);
  return output;
}

std::optional<bridge::BridgeBackupRestoreCancellationRequest>
ToBridgeBackupRestoreCancellationRequest(
    const mojom::BackupRestoreCancellationRequest& input) {
  std::optional<bridge::BridgeBackupOperation> operation =
      input.operation ? ToBridgeBackupOperation(*input.operation)
                      : std::nullopt;
  std::optional<bridge::BridgeBackupRestoreBinding> binding =
      ToBinding(input.binding.get());
  if (!operation || !binding) {
    return std::nullopt;
  }
  bridge::BridgeBackupRestoreCancellationRequest output;
  output.operation = std::move(*operation);
  output.binding = std::move(*binding);
  return output;
}

}  // namespace taffy::core_service_internal
