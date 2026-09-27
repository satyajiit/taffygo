// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <utility>

#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/backup_restore_recovery_journal_internal.h"
#include "taffy/browser/core_backup_planning_validation.h"

namespace taffy {
namespace {

namespace wire = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;

std::optional<wire::BackupRestorePhysicalIntent> PhysicalIntent(
    wire::BackupRestoreResolutionChoice choice) {
  switch (choice) {
    case wire::BackupRestoreResolutionChoice::kAcceptCandidate:
      return wire::BackupRestorePhysicalIntent::kAcceptCandidate;
    case wire::BackupRestoreResolutionChoice::kDiscardCandidate:
      return wire::BackupRestorePhysicalIntent::kDiscardCandidate;
  }
  return std::nullopt;
}

std::optional<wire::BackupRestoreObservedOutcome> Observed(
    wire::BackupRestoreResolutionOutcome outcome) {
  switch (outcome) {
    case wire::BackupRestoreResolutionOutcome::kCompleted:
      return wire::BackupRestoreObservedOutcome::kCompleted;
    case wire::BackupRestoreResolutionOutcome::kDefinitelyNotCompleted:
      return wire::BackupRestoreObservedOutcome::kDefinitelyNotCompleted;
    case wire::BackupRestoreResolutionOutcome::kOutcomeUnknown:
      return wire::BackupRestoreObservedOutcome::kOutcomeUnknown;
  }
  return std::nullopt;
}

}  // namespace

BackupRestoreRecoveryRecordResult BeginBackupRestoreResolutionIntent(
    PrefService* local_state,
    const std::string& reservation_id,
    const wire::BackupRestoreRecoveryResolutionAuthorization& authorization) {
  auto intent = PhysicalIntent(authorization.choice);
  if (!intent || !authorization.binding ||
      authorization.binding->reservation_id != reservation_id ||
      !authorization.decision_operation ||
      !IsLiveBackupOperation(
          authorization.decision_operation.get(),
          authorization.decision_operation->service_generation, 0u) ||
      authorization.history_prefix.size() < 2u ||
      authorization.history_prefix.size() >
          wire::kMaxBackupRestoreRecoveryRecords - 3u) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  if (!history) {
    return base::unexpected(history.error());
  }
  if (!backup_restore_recovery_journal_internal::IsExactHistory(
          *history, authorization.history_prefix)) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  auto expected_binding =
      backup_restore_recovery_codec::EncodeBinding(*history->front()->binding);
  auto received_binding =
      backup_restore_recovery_codec::EncodeBinding(*authorization.binding);
  if (!expected_binding || !received_binding ||
      *expected_binding != *received_binding) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto record = wire::BackupRestoreRecoveryRecord::New();
  record->format_version = 1u;
  record->sequence = history->size() + 1u;
  record->binding = authorization.binding.Clone();
  record->fact_kind = wire::BackupRestoreRecoveryFactKind::kIntentRecorded;
  record->intent = wire::BackupRestoreRecoveryIntentFact::New(
      authorization.intent_id, *intent);
  auto appended = backup_restore_recovery_journal_internal::Append(
      local_state, reservation_id, *record);
  return appended ? BackupRestoreRecoveryRecordResult(std::move(record))
                  : base::unexpected(appended.error());
}

BackupRestoreRecoveryRecordResult RecordBackupRestoreResolutionOutcome(
    PrefService* local_state,
    const std::string& reservation_id,
    const wire::BackupRestoreRecoveryRecord& exact_intent,
    wire::BackupRestoreResolutionOutcome outcome) {
  auto encoded = EncodeBackupRestoreRecoveryRecord(exact_intent);
  auto observed = Observed(outcome);
  if (!encoded || !observed || exact_intent.sequence <= 1u ||
      !exact_intent.intent ||
      (exact_intent.intent->intent !=
           wire::BackupRestorePhysicalIntent::kAcceptCandidate &&
       exact_intent.intent->intent !=
           wire::BackupRestorePhysicalIntent::kDiscardCandidate)) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  if (!history) {
    return base::unexpected(history.error());
  }
  if (exact_intent.sequence > history->size() ||
      history->size() - exact_intent.sequence > 2u) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  auto stored = EncodeBackupRestoreRecoveryRecord(
      *history->at(exact_intent.sequence - 1u));
  if (!stored || *stored != *encoded) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  for (size_t i = exact_intent.sequence; i < history->size(); ++i) {
    const auto& fact = history->at(i)->outcome;
    if (!fact || fact->intent_id != exact_intent.intent->intent_id ||
        (i + 1u < history->size() &&
         fact->outcome !=
             wire::BackupRestoreObservedOutcome::kOutcomeUnknown)) {
      return base::unexpected(Error::kWrongPhysicalState);
    }
  }
  if (history->size() > exact_intent.sequence) {
    const auto previous = history->back()->outcome->outcome;
    if (previous == *observed) {
      return history->back().Clone();
    }
    if (previous != wire::BackupRestoreObservedOutcome::kOutcomeUnknown ||
        history->size() != exact_intent.sequence + 1u) {
      return base::unexpected(Error::kWrongPhysicalState);
    }
  }
  auto record = wire::BackupRestoreRecoveryRecord::New();
  record->format_version = exact_intent.format_version;
  record->sequence = history->size() + 1u;
  record->binding = exact_intent.binding.Clone();
  record->fact_kind = wire::BackupRestoreRecoveryFactKind::kOutcomeObserved;
  record->outcome = wire::BackupRestoreRecoveryOutcomeFact::New(
      exact_intent.intent->intent_id, *observed);
  auto appended = backup_restore_recovery_journal_internal::Append(
      local_state, reservation_id, *record);
  return appended ? BackupRestoreRecoveryRecordResult(std::move(record))
                  : base::unexpected(appended.error());
}

}  // namespace taffy
