// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_backup_recovery_validation.h"

#include <algorithm>
#include <functional>
#include <string_view>

#include "base/strings/string_util.h"
#include "taffy/browser/core_backup_planning_validation.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

bool IsBoundedWireText(std::string_view value) {
  return value.size() <= core_mojom::kMaxBackupIdBytes &&
         base::IsStringUTF8(value);
}

bool IsBoundedIdentity(std::string_view value) {
  return !value.empty() && IsBoundedWireText(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

template <typename Range>
bool IsDigest(const Range& digest) {
  return digest.size() == 32u;
}

bool IsKnownRecordKind(core_mojom::BackupRecordKind kind) {
  switch (kind) {
    case core_mojom::BackupRecordKind::kAssistantConfiguration:
    case core_mojom::BackupRecordKind::kSavedWorkspace:
    case core_mojom::BackupRecordKind::kLibraryEntry:
    case core_mojom::BackupRecordKind::kMemoryRecord:
    case core_mojom::BackupRecordKind::kUserAuthoredSkill:
    case core_mojom::BackupRecordKind::kLearnedProcedure:
    case core_mojom::BackupRecordKind::kBookmark:
    case core_mojom::BackupRecordKind::kBrowserPreference:
      return true;
  }
  return false;
}

bool IsKnownFactKind(core_mojom::BackupRestoreRecoveryFactKind kind) {
  switch (kind) {
    case core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded:
    case core_mojom::BackupRestoreRecoveryFactKind::kOutcomeObserved:
      return true;
  }
  return false;
}

bool IsKnownIntent(core_mojom::BackupRestorePhysicalIntent intent) {
  switch (intent) {
    case core_mojom::BackupRestorePhysicalIntent::kCommitCandidate:
    case core_mojom::BackupRestorePhysicalIntent::kAcceptCandidate:
    case core_mojom::BackupRestorePhysicalIntent::kDiscardCandidate:
      return true;
  }
  return false;
}

bool IsKnownOutcome(core_mojom::BackupRestoreObservedOutcome outcome) {
  switch (outcome) {
    case core_mojom::BackupRestoreObservedOutcome::kCompleted:
    case core_mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted:
    case core_mojom::BackupRestoreObservedOutcome::kOutcomeUnknown:
      return true;
  }
  return false;
}

bool IsKnownInspectionStatus(
    core_mojom::BackupRestoreRecoveryInspectionStatus status) {
  switch (status) {
    case core_mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded:
    case core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidOperation:
    case core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidRecord:
    case core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidHistory:
    case core_mojom::BackupRestoreRecoveryInspectionStatus::kUnavailable:
      return true;
  }
  return false;
}

bool IsKnownClassification(
    core_mojom::BackupRestoreRecoveryClassificationKind kind) {
  switch (kind) {
    case core_mojom::BackupRestoreRecoveryClassificationKind::
        kReconcileRequired:
    case core_mojom::BackupRestoreRecoveryClassificationKind::
        kRollbackAvailable:
    case core_mojom::BackupRestoreRecoveryClassificationKind::kCleanupRequired:
    case core_mojom::BackupRestoreRecoveryClassificationKind::kPublished:
    case core_mojom::BackupRestoreRecoveryClassificationKind::kVerifiedDeleted:
      return true;
  }
  return false;
}

bool IsKnownError(core_mojom::BackupRestoreRecoveryError error) {
  switch (error) {
    case core_mojom::BackupRestoreRecoveryError::kMissingCommitIntent:
    case core_mojom::BackupRestoreRecoveryError::kTooManyRecords:
    case core_mojom::BackupRestoreRecoveryError::kUnsupportedVersion:
    case core_mojom::BackupRestoreRecoveryError::kInvalidSequence:
    case core_mojom::BackupRestoreRecoveryError::kInvalidBinding:
    case core_mojom::BackupRestoreRecoveryError::kBindingChanged:
    case core_mojom::BackupRestoreRecoveryError::kInvalidIntentId:
    case core_mojom::BackupRestoreRecoveryError::kIntentIdReused:
    case core_mojom::BackupRestoreRecoveryError::kUnexpectedIntent:
    case core_mojom::BackupRestoreRecoveryError::kUnresolvedIntent:
    case core_mojom::BackupRestoreRecoveryError::kOutcomeWithoutIntent:
    case core_mojom::BackupRestoreRecoveryError::kOutcomeIntentMismatch:
    case core_mojom::BackupRestoreRecoveryError::kDuplicateUnknownOutcome:
    case core_mojom::BackupRestoreRecoveryError::kTerminalHistoryExtended:
      return true;
  }
  return false;
}

bool IsValidBindingShapeImpl(
    const core_mojom::BackupRestoreRecoveryBinding* binding,
    std::string_view expected_owner_profile_id) {
  if (!binding || binding->owner_profile_id != expected_owner_profile_id ||
      !IsBoundedIdentity(binding->reservation_id) ||
      !IsBoundedIdentity(binding->owner_profile_id) ||
      !IsBoundedIdentity(binding->target_profile_id) ||
      !IsBoundedIdentity(binding->backup_id) ||
      binding->target_kind !=
          core_mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      binding->owner_profile_id == binding->target_profile_id ||
      !IsDigest(binding->snapshot_sha256) ||
      !IsDigest(binding->confirmation_sha256) ||
      !IsDigest(binding->candidate_records_sha256) ||
      std::ranges::all_of(binding->snapshot_sha256,
                          [](uint8_t value) { return value == 0u; }) ||
      std::ranges::all_of(binding->confirmation_sha256,
                          [](uint8_t value) { return value == 0u; }) ||
      std::ranges::all_of(binding->candidate_records_sha256,
                          [](uint8_t value) { return value == 0u; }) ||
      binding->selection.size() > core_mojom::kMaxBackupSelectionKinds ||
      binding->record_count > core_mojom::kMaxBackupRecords) {
    return false;
  }
  const bool canonical =
      std::ranges::all_of(binding->selection, IsKnownRecordKind) &&
      std::adjacent_find(binding->selection.begin(), binding->selection.end(),
                         std::greater_equal<>()) == binding->selection.end();
  return canonical &&
         ((binding->record_count == 0u && binding->selection.empty()) ||
          (binding->record_count >= binding->selection.size() &&
           !binding->selection.empty()));
}

bool IsValidRecordShapeImpl(
    const core_mojom::BackupRestoreRecoveryRecord* record,
    std::string_view expected_owner_profile_id) {
  if (!record ||
      !IsValidBindingShapeImpl(record->binding.get(),
                               expected_owner_profile_id) ||
      !IsKnownFactKind(record->fact_kind)) {
    return false;
  }
  const bool has_intent = !!record->intent;
  const bool has_outcome = !!record->outcome;
  if ((record->fact_kind ==
           core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded &&
       (!has_intent || has_outcome)) ||
      (record->fact_kind ==
           core_mojom::BackupRestoreRecoveryFactKind::kOutcomeObserved &&
       (has_intent || !has_outcome))) {
    return false;
  }
  if (record->intent && (!IsBoundedIdentity(record->intent->intent_id) ||
                         !IsKnownIntent(record->intent->intent))) {
    return false;
  }
  return !record->outcome || (IsBoundedIdentity(record->outcome->intent_id) &&
                              IsKnownOutcome(record->outcome->outcome));
}

}  // namespace

bool IsValidBackupRestoreRecoveryBindingShape(
    const core_mojom::BackupRestoreRecoveryBinding* binding,
    std::string_view expected_owner_profile_id) {
  return IsValidBindingShapeImpl(binding, expected_owner_profile_id);
}

bool IsExactBackupRestoreRecoveryBinding(
    const core_mojom::BackupRestoreRecoveryBinding* expected,
    const core_mojom::BackupRestoreRecoveryBinding* received) {
  return expected && received && expected->Equals(*received);
}

bool IsValidBackupRestoreRecoveryRecordShape(
    const core_mojom::BackupRestoreRecoveryRecord* record,
    std::string_view expected_owner_profile_id) {
  return IsValidRecordShapeImpl(record, expected_owner_profile_id);
}

bool IsExactBackupRestoreRecoveryHistory(
    const std::vector<core_mojom::BackupRestoreRecoveryRecordPtr>& expected,
    const std::vector<core_mojom::BackupRestoreRecoveryRecordPtr>& received) {
  return expected.size() == received.size() &&
         std::ranges::equal(expected, received,
                            [](const auto& left, const auto& right) {
                              return left && right && left->Equals(*right);
                            });
}

bool IsValidBackupRestoreRecoveryInspectionRequest(
    const core_mojom::BackupRestoreRecoveryInspectionRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::string_view expected_owner_profile_id) {
  return IsBoundedIdentity(expected_owner_profile_id) &&
         IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         request.records.size() <=
             core_mojom::kMaxBackupRestoreRecoveryRecords &&
         std::ranges::all_of(request.records, [&](const auto& record) {
           return IsValidRecordShapeImpl(record.get(),
                                         expected_owner_profile_id);
         });
}

bool IsValidBackupRestoreRecoveryInspectionResult(
    const core_mojom::OperationEnvelope& expected_operation,
    const core_mojom::BackupRestoreRecoveryInspectionResult& result) {
  if (!IsExactBackupOperation(&expected_operation, result.operation.get()) ||
      !IsKnownInspectionStatus(result.status)) {
    return false;
  }
  const bool succeeded =
      result.status ==
      core_mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded;
  const bool invalid_history =
      result.status ==
      core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidHistory;
  if (static_cast<bool>(result.classification) != succeeded ||
      static_cast<bool>(result.failure) != invalid_history) {
    return false;
  }
  if (result.failure && !IsKnownError(result.failure->error)) {
    return false;
  }
  if (!result.classification) {
    return true;
  }
  const bool reconcile =
      result.classification->kind ==
      core_mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired;
  if (!IsKnownClassification(result.classification->kind) ||
      static_cast<bool>(result.classification->reconciliation) != reconcile) {
    return false;
  }
  return !result.classification->reconciliation ||
         (IsBoundedIdentity(result.classification->reconciliation->intent_id) &&
          IsKnownIntent(result.classification->reconciliation->intent));
}

}  // namespace taffy
