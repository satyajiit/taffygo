// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_recovery_journal.h"

#include <optional>
#include <utility>

#include "components/prefs/pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_recovery_journal_internal.h"
#include "taffy/browser/core_backup_protocol_validation.h"

namespace taffy {
namespace {

namespace wire = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;
using backup_restore_recovery_journal_internal::Append;

base::expected<BackupRestoreProfileReservation, Error> Reservation(
    const PrefService* local_state,
    const std::string& reservation_id) {
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  if (!reservations) {
    return base::unexpected(reservations.error());
  }
  if (reservations->size() != 1u ||
      reservations->front().reservation_id != reservation_id) {
    return base::unexpected(Error::kNotFound);
  }
  return reservations->front();
}

std::optional<wire::BackupRestoreObservedOutcome> Observed(
    wire::BackupRestoreCommitOutcome outcome) {
  switch (outcome) {
    case wire::BackupRestoreCommitOutcome::kCommitted:
      return wire::BackupRestoreObservedOutcome::kCompleted;
    case wire::BackupRestoreCommitOutcome::kDefinitelyNotCommitted:
      return wire::BackupRestoreObservedOutcome::kDefinitelyNotCompleted;
    case wire::BackupRestoreCommitOutcome::kOutcomeUnknown:
      return wire::BackupRestoreObservedOutcome::kOutcomeUnknown;
  }
  return std::nullopt;
}

}  // namespace

base::expected<BackupRestoreRecoveryRecords, Error>
ReadBackupRestoreRecoveryJournal(const PrefService* local_state,
                                 const std::string& reservation_id) {
  auto reservation = Reservation(local_state, reservation_id);
  if (!reservation) {
    return base::unexpected(reservation.error());
  }
  const auto* entry =
      local_state
          ->GetDict(application_preferences::kBackupRestoreProfileReservations)
          .FindDict(reservation_id);
  const auto* value =
      entry ? entry->Find(kBackupRestoreRecoveryJournalKey) : nullptr;
  if (!value) {
    return BackupRestoreRecoveryRecords();
  }
  if (!reservation->target_profile_id ||
      reservation->physical_state !=
          BackupRestoreProfilePhysicalState::kProfileCreated) {
    return base::unexpected(Error::kCorrupt);
  }
  return DecodeBackupRestoreRecoveryJournal(*value, reservation_id,
                                            *reservation->target_profile_id);
}

BackupRestoreRecoveryRecordResult BeginBackupRestoreCommitIntent(
    PrefService* local_state,
    const std::string& reservation_id,
    const wire::BackupRestoreBinding& binding,
    const wire::BackupRestoreCandidateWitness& witness,
    std::string intent_id) {
  auto reservation = Reservation(local_state, reservation_id);
  if (!reservation) {
    return base::unexpected(reservation.error());
  }
  if (!binding.planning_operation ||
      !IsValidBackupRestoreBinding(
          &binding, binding.planning_operation->service_generation) ||
      reservation->physical_state !=
          BackupRestoreProfilePhysicalState::kProfileCreated ||
      !reservation->target_profile_id ||
      *reservation->target_profile_id != binding.target->profile_id) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  if (!history) {
    return base::unexpected(history.error());
  }
  if (!history->empty()) {
    return base::unexpected(Error::kBusy);
  }
  auto recovery = wire::BackupRestoreRecoveryBinding::New();
  recovery->reservation_id = reservation_id;
  recovery->owner_profile_id = binding.owner_profile_id;
  recovery->target_kind = binding.target->kind;
  recovery->target_profile_id = binding.target->profile_id;
  recovery->backup_id = binding.backup_id;
  recovery->snapshot_sha256 = binding.snapshot_sha256;
  recovery->confirmation_sha256 = binding.confirmation_sha256;
  recovery->selection = witness.selection;
  recovery->record_count = witness.record_count;
  recovery->candidate_records_sha256 = witness.candidate_records_sha256;
  auto record = wire::BackupRestoreRecoveryRecord::New();
  record->format_version = 1u;
  record->sequence = 1u;
  record->binding = std::move(recovery);
  record->fact_kind = wire::BackupRestoreRecoveryFactKind::kIntentRecorded;
  record->intent = wire::BackupRestoreRecoveryIntentFact::New(
      std::move(intent_id),
      wire::BackupRestorePhysicalIntent::kCommitCandidate);
  auto appended = Append(local_state, reservation_id, *record);
  return appended ? BackupRestoreRecoveryRecordResult(std::move(record))
                  : base::unexpected(appended.error());
}

BackupRestoreRecoveryRecordResult RecordBackupRestoreCommitOutcome(
    PrefService* local_state,
    const std::string& reservation_id,
    const wire::BackupRestoreRecoveryRecord& exact_intent,
    wire::BackupRestoreCommitOutcome outcome) {
  auto encoded_intent = EncodeBackupRestoreRecoveryRecord(exact_intent);
  auto observed = Observed(outcome);
  if (!encoded_intent || !observed || exact_intent.sequence != 1u ||
      !exact_intent.intent ||
      exact_intent.intent->intent !=
          wire::BackupRestorePhysicalIntent::kCommitCandidate) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  if (!history) {
    return base::unexpected(history.error());
  }
  if (history->empty() || history->size() > 3u) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  auto first = EncodeBackupRestoreRecoveryRecord(*history->front());
  if (!first || *first != *encoded_intent) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  // This narrow physical owner records only its one commit, never resolution
  // policy. The Rust inspector independently validates the full history.
  for (size_t i = 1u; i < history->size(); ++i) {
    const auto& fact = history->at(i)->outcome;
    if (!fact || fact->intent_id != exact_intent.intent->intent_id ||
        (i + 1u < history->size() &&
         fact->outcome !=
             wire::BackupRestoreObservedOutcome::kOutcomeUnknown)) {
      return base::unexpected(Error::kWrongPhysicalState);
    }
  }
  if (history->size() > 1u) {
    const auto previous = history->back()->outcome->outcome;
    if (previous == *observed) {
      return history->back().Clone();
    }
    if (previous != wire::BackupRestoreObservedOutcome::kOutcomeUnknown ||
        history->size() != 2u) {
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
  auto appended = Append(local_state, reservation_id, *record);
  return appended ? BackupRestoreRecoveryRecordResult(std::move(record))
                  : base::unexpected(appended.error());
}

}  // namespace taffy
