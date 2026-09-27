// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_reservation_retirement.h"

#include <optional>
#include <utility>

#include "base/check.h"
#include "base/no_destructor.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_recovery_journal_internal.h"

namespace taffy {
namespace {

namespace wire = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;

struct RetirementFence {
  base::Token id;
  base::FilePath target_base_name;
};

std::optional<RetirementFence>& Fence() {
  static base::NoDestructor<std::optional<RetirementFence>> fence;
  return *fence;
}

// Correlates the terminal observation with the physical kind the source Core
// classified. This is intentionally not a second history reducer: all earlier
// semantic transitions must already have been accepted by the source Core.
bool TerminalMatches(const BackupRestoreRecoveryRecords& history,
                     const wire::BackupRestoreRecoveryClassification& state) {
  if (history.size() < 4u || state.reconciliation || !history.back()->outcome ||
      history.back()->outcome->outcome !=
          wire::BackupRestoreObservedOutcome::kCompleted) {
    return false;
  }
  size_t intent_index = history.size() - 2u;
  const auto& completed = *history.back()->outcome;
  if (history[intent_index]->outcome) {
    const auto& unknown = *history[intent_index]->outcome;
    if (unknown.outcome !=
            wire::BackupRestoreObservedOutcome::kOutcomeUnknown ||
        unknown.intent_id != completed.intent_id) {
      return false;
    }
    --intent_index;
  }
  const auto& intent = history[intent_index]->intent;
  if (!intent || intent->intent_id != completed.intent_id) {
    return false;
  }
  return (state.kind ==
              wire::BackupRestoreRecoveryClassificationKind::kPublished &&
          intent->intent ==
              wire::BackupRestorePhysicalIntent::kAcceptCandidate) ||
         (state.kind ==
              wire::BackupRestoreRecoveryClassificationKind::kVerifiedDeleted &&
          intent->intent ==
              wire::BackupRestorePhysicalIntent::kDiscardCandidate);
}

base::expected<void, Error> ValidateTerminalRetirement(
    PrefService* local_state,
    const BackupRestoreProfileReservation& exact_reservation,
    const BackupRestoreRecoveryRecords& exact_terminal_history,
    const wire::BackupRestoreRecoveryClassification& classification) {
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  if (!reservations) {
    return base::unexpected(reservations.error());
  }
  if (reservations->size() != 1u ||
      reservations->front() != exact_reservation ||
      exact_reservation.physical_state !=
          BackupRestoreProfilePhysicalState::kProfileCreated ||
      !exact_reservation.target_profile_id) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  auto history = ReadBackupRestoreRecoveryJournal(
      local_state, exact_reservation.reservation_id);
  if (!history) {
    return base::unexpected(history.error());
  }
  if (!backup_restore_recovery_journal_internal::IsExactHistory(
          *history, exact_terminal_history) ||
      !TerminalMatches(*history, classification)) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  const auto* preference = local_state->FindPreference(
      application_preferences::kBackupRestoreProfileReservations);
  if (!preference || !preference->IsUserModifiable()) {
    return base::unexpected(Error::kUnavailable);
  }
  return base::ok();
}

base::expected<void, Error> ValidatePristinePrecommitRetirement(
    PrefService* local_state,
    const BackupRestoreProfileReservation& exact_reservation) {
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  if (!reservations) {
    return base::unexpected(reservations.error());
  }
  if (reservations->size() != 1u ||
      reservations->front() != exact_reservation ||
      exact_reservation.physical_state !=
          BackupRestoreProfilePhysicalState::kProfileCreated) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  auto history = ReadBackupRestoreRecoveryJournal(
      local_state, exact_reservation.reservation_id);
  if (!history || !history->empty()) {
    return base::unexpected(history ? Error::kWrongPhysicalState
                                    : history.error());
  }
  const auto* preference = local_state->FindPreference(
      application_preferences::kBackupRestoreProfileReservations);
  if (!preference || !preference->IsUserModifiable()) {
    return base::unexpected(Error::kUnavailable);
  }
  return base::ok();
}

bool HasExactAbsence(const PrefService* local_state) {
  const auto* preference = local_state->FindPreference(
      application_preferences::kBackupRestoreProfileReservations);
  const auto reservations = ReadBackupRestoreProfileReservations(local_state);
  return preference && preference->IsUserModifiable() &&
         !preference->HasUserSetting() && reservations && reservations->empty();
}

}  // namespace

base::expected<std::unique_ptr<BackupRestoreReservationRetirement>, Error>
BackupRestoreReservationRetirement::Begin(
    PrefService* local_state,
    const BackupRestoreProfileReservation& exact_reservation,
    const BackupRestoreRecoveryRecords& exact_terminal_history,
    const wire::BackupRestoreRecoveryClassification& classification) {
  if (IsInProgress()) {
    return base::unexpected(Error::kBusy);
  }
  auto valid = ValidateTerminalRetirement(
      local_state, exact_reservation, exact_terminal_history, classification);
  if (!valid) {
    return base::unexpected(valid.error());
  }
  auto owner = std::unique_ptr<BackupRestoreReservationRetirement>(
      new BackupRestoreReservationRetirement(
          local_state, exact_reservation.target_profile_base_name,
          local_state
              ->GetDict(
                  application_preferences::kBackupRestoreProfileReservations)
              .Clone()));
  // An explicitly stored empty registry is corrupt. ClearPref is the sole
  // valid empty representation. Its synchronous observers see the fence.
  local_state->ClearPref(
      application_preferences::kBackupRestoreProfileReservations);
  return owner;
}

base::expected<std::unique_ptr<BackupRestoreReservationRetirement>, Error>
BackupRestoreReservationRetirement::BeginPristinePrecommitCleanup(
    PrefService* local_state,
    const BackupRestoreProfileReservation& exact_reservation) {
  if (IsInProgress()) {
    return base::unexpected(Error::kBusy);
  }
  auto valid =
      ValidatePristinePrecommitRetirement(local_state, exact_reservation);
  if (!valid) {
    return base::unexpected(valid.error());
  }
  auto owner = std::unique_ptr<BackupRestoreReservationRetirement>(
      new BackupRestoreReservationRetirement(
          local_state, exact_reservation.target_profile_base_name,
          local_state
              ->GetDict(
                  application_preferences::kBackupRestoreProfileReservations)
              .Clone()));
  local_state->ClearPref(
      application_preferences::kBackupRestoreProfileReservations);
  return owner;
}

BackupRestoreReservationRetirement::BackupRestoreReservationRetirement(
    PrefService* local_state,
    base::FilePath target_profile_base_name,
    base::DictValue prior_registry)
    : local_state_(local_state), prior_registry_(std::move(prior_registry)) {
  CHECK(!Fence());
  Fence() = RetirementFence{fence_id_, std::move(target_profile_base_name)};
}

BackupRestoreReservationRetirement::~BackupRestoreReservationRetirement() {
  if (released_ || !Fence() || Fence()->id != fence_id_) {
    return;
  }
  if (HasExactAbsence(local_state_)) {
    local_state_->SetDict(
        application_preferences::kBackupRestoreProfileReservations,
        prior_registry_.Clone());
  }
  const auto reservations = ReadBackupRestoreProfileReservations(local_state_);
  if (reservations &&
      local_state_->GetDict(
          application_preferences::kBackupRestoreProfileReservations) ==
          prior_registry_) {
    Fence().reset();
  }
}

bool BackupRestoreReservationRetirement::ReleaseAfterVerifiedAbsence() {
  if (released_ || !Fence() || Fence()->id != fence_id_ ||
      !HasExactAbsence(local_state_)) {
    return false;
  }
  released_ = true;
  Fence().reset();
  return true;
}

bool BackupRestoreReservationRetirement::IsInProgress() {
  return Fence().has_value();
}

bool BackupRestoreReservationRetirement::BlocksProfileBaseName(
    const base::FilePath& profile_base_name) {
  return Fence() && Fence()->target_base_name == profile_base_name;
}

}  // namespace taffy
