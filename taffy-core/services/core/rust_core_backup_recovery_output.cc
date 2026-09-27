// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>
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

bool IsBoundedText(const rust::String& input, uint64_t maximum) {
  const std::string value(input);
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

bool IsValidOperation(const bridge::BridgeBackupOperation& operation) {
  return IsBoundedText(operation.operation_id,
                       core_mojom::kMaxOperationIdBytes) &&
         IsBoundedText(operation.idempotency_key,
                       core_mojom::kMaxIdempotencyKeyBytes);
}

core_mojom::OperationEnvelopePtr ToOperation(
    const bridge::BridgeBackupOperation& input) {
  return core_mojom::OperationEnvelope::New(
      std::string(input.operation_id), input.service_generation,
      input.task_revision, input.deadline_monotonic_ms,
      std::string(input.idempotency_key));
}

}  // namespace

core_mojom::BackupRestoreRecoveryBindingPtr
ToMojoBackupRestoreRecoveryBinding(
    bridge::BridgeBackupRestoreRecoveryBinding input) {
  const auto target_kind =
      wire::BackupRestoreTargetKindFromWire(input.target_kind);
  if (!target_kind ||
      !IsBoundedText(input.reservation_id, core_mojom::kMaxBackupIdBytes) ||
      !IsBoundedText(input.owner_profile_id, core_mojom::kMaxBackupIdBytes) ||
      !IsBoundedText(input.target_profile_id, core_mojom::kMaxBackupIdBytes) ||
      !IsBoundedText(input.backup_id, core_mojom::kMaxBackupIdBytes) ||
      input.selection.size() > core_mojom::kMaxBackupSelectionKinds ||
      std::ranges::all_of(input.snapshot_sha256,
                          [](uint8_t byte) { return byte == 0u; }) ||
      std::ranges::all_of(input.confirmation_sha256,
                          [](uint8_t byte) { return byte == 0u; }) ||
      std::ranges::all_of(input.candidate_records_sha256,
                          [](uint8_t byte) { return byte == 0u; })) {
    return nullptr;
  }
  std::vector<core_mojom::BackupRecordKind> selection;
  selection.reserve(input.selection.size());
  for (const uint8_t kind : input.selection) {
    const auto value = wire::BackupRecordKindFromWire(kind);
    if (!value) {
      return nullptr;
    }
    selection.push_back(*value);
  }
  auto output = core_mojom::BackupRestoreRecoveryBinding::New();
  output->reservation_id = std::string(input.reservation_id);
  output->owner_profile_id = std::string(input.owner_profile_id);
  output->target_kind = *target_kind;
  output->target_profile_id = std::string(input.target_profile_id);
  output->backup_id = std::string(input.backup_id);
  output->snapshot_sha256.assign(input.snapshot_sha256.begin(),
                                 input.snapshot_sha256.end());
  output->confirmation_sha256.assign(input.confirmation_sha256.begin(),
                                     input.confirmation_sha256.end());
  output->selection = std::move(selection);
  output->record_count = input.record_count;
  output->candidate_records_sha256.assign(
      input.candidate_records_sha256.begin(),
      input.candidate_records_sha256.end());
  return output;
}

core_mojom::BackupRestoreRecoveryRecordPtr
ToMojoBackupRestoreRecoveryRecord(
    bridge::BridgeBackupRestoreRecoveryRecord input) {
  const auto fact_kind =
      wire::BackupRestoreRecoveryFactKindFromWire(input.fact_kind);
  const auto intent =
      wire::BackupRestorePhysicalIntentFromWire(input.intent.intent);
  const auto outcome =
      wire::BackupRestoreObservedOutcomeFromWire(input.outcome.outcome);
  auto binding = ToMojoBackupRestoreRecoveryBinding(std::move(input.binding));
  if (!fact_kind || !intent || !outcome || !binding ||
      input.has_intent !=
          (*fact_kind ==
           core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded) ||
      input.has_outcome !=
          (*fact_kind ==
           core_mojom::BackupRestoreRecoveryFactKind::kOutcomeObserved) ||
      (input.has_intent &&
       !IsBoundedText(input.intent.intent_id,
                      core_mojom::kMaxBackupIdBytes)) ||
      (input.has_outcome &&
       !IsBoundedText(input.outcome.intent_id,
                      core_mojom::kMaxBackupIdBytes))) {
    return nullptr;
  }
  core_mojom::BackupRestoreRecoveryIntentFactPtr intent_body;
  if (input.has_intent) {
    intent_body = core_mojom::BackupRestoreRecoveryIntentFact::New(
        std::string(input.intent.intent_id), *intent);
  }
  core_mojom::BackupRestoreRecoveryOutcomeFactPtr outcome_body;
  if (input.has_outcome) {
    outcome_body = core_mojom::BackupRestoreRecoveryOutcomeFact::New(
        std::string(input.outcome.intent_id), *outcome);
  }
  return core_mojom::BackupRestoreRecoveryRecord::New(
      input.format_version, input.sequence, std::move(binding), *fact_kind,
      std::move(intent_body), std::move(outcome_body));
}

core_mojom::BackupRestoreRecoveryInspectionResultPtr
ToMojoBackupRestoreRecoveryInspectionResult(
    bridge::BridgeBackupRestoreRecoveryInspectionResult input) {
  const auto status =
      wire::BackupRestoreRecoveryInspectionStatusFromWire(input.status);
  const auto classification_kind =
      wire::BackupRestoreRecoveryClassificationKindFromWire(
          input.classification.kind);
  const auto reconciliation_intent = wire::BackupRestorePhysicalIntentFromWire(
      input.classification.reconciliation.intent);
  const auto error =
      wire::BackupRestoreRecoveryErrorFromWire(input.failure.error);
  if (!status || !classification_kind || !reconciliation_intent || !error ||
      !IsValidOperation(input.operation) ||
      input.has_classification !=
          (*status ==
           core_mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded) ||
      input.has_failure !=
          (*status == core_mojom::BackupRestoreRecoveryInspectionStatus::
                          kInvalidHistory) ||
      input.classification.has_reconciliation !=
          (*classification_kind ==
           core_mojom::BackupRestoreRecoveryClassificationKind::
               kReconcileRequired) ||
      (input.classification.has_reconciliation &&
       !IsBoundedText(input.classification.reconciliation.intent_id,
                      core_mojom::kMaxBackupIdBytes))) {
    return nullptr;
  }
  auto output = core_mojom::BackupRestoreRecoveryInspectionResult::New();
  output->operation = ToOperation(input.operation);
  output->status = *status;
  if (input.has_classification) {
    core_mojom::BackupRestoreRecoveryReconciliationPtr reconciliation;
    if (input.classification.has_reconciliation) {
      reconciliation = core_mojom::BackupRestoreRecoveryReconciliation::New(
          std::string(input.classification.reconciliation.intent_id),
          *reconciliation_intent);
    }
    output->classification =
        core_mojom::BackupRestoreRecoveryClassification::New(
            *classification_kind, std::move(reconciliation));
  }
  if (input.has_failure) {
    output->failure = core_mojom::BackupRestoreRecoveryFailure::New(*error);
  }
  return output;
}

}  // namespace taffy::core_service_internal
