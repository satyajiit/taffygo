// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "components/prefs/pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_codec.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"

namespace taffy {
namespace {

using Error = BackupRestoreProfileRegistryError;
using PhysicalState = BackupRestoreProfilePhysicalState;

constexpr int kLegacyRegistryEntryVersion = 1;
constexpr int kRegistryEntryVersion = 2;
constexpr char kVersionKey[] = "version";
constexpr char kRecoveryPresentationKey[] = "recovery_presentation";

base::expected<BackupRestoreProfileReservation, Error> FindReservation(
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

base::expected<base::DictValue, Error> MutableRegistry(
    PrefService* local_state) {
  const PrefService::Preference* preference =
      local_state
          ? local_state->FindPreference(
                application_preferences::kBackupRestoreProfileReservations)
          : nullptr;
  if (!preference || !preference->IsUserModifiable()) {
    return base::unexpected(Error::kUnavailable);
  }
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  if (!reservations) {
    return base::unexpected(reservations.error());
  }
  return local_state
      ->GetDict(application_preferences::kBackupRestoreProfileReservations)
      .Clone();
}

}  // namespace

BackupRestoreRecoveryPresentationResult AttachBackupRestoreRecoveryPresentation(
    PrefService* local_state,
    const std::string& reservation_id,
    std::u16string target_profile_label,
    base::span<const core_service::mojom::BackupRecordKind> original_selection,
    const core_service::mojom::BackupRestorePlanResult& exact_plan) {
  auto presentation = backup_restore_recovery_presentation_internal::Build(
      std::move(target_profile_label), original_selection, exact_plan);
  if (!presentation) {
    return base::unexpected(presentation.error());
  }
  auto reservation = FindReservation(local_state, reservation_id);
  if (!reservation) {
    return base::unexpected(reservation.error());
  }
  if (reservation->physical_state != PhysicalState::kProfileCreated ||
      !reservation->target_profile_id ||
      *reservation->target_profile_id != presentation->target_profile_id) {
    return base::unexpected(Error::kWrongPhysicalState);
  }

  const base::DictValue& current_registry = local_state->GetDict(
      application_preferences::kBackupRestoreProfileReservations);
  const base::DictValue* current_entry =
      current_registry.FindDict(reservation_id);
  if (!current_entry || current_entry->Find(kBackupRestoreRecoveryJournalKey)) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  if (const base::Value* current =
          current_entry->Find(kRecoveryPresentationKey)) {
    auto decoded =
        backup_restore_recovery_presentation_internal::Decode(*current);
    return decoded && *decoded == *presentation
               ? BackupRestoreRecoveryPresentationResult(std::move(*decoded))
               : base::unexpected(Error::kWrongPhysicalState);
  }

  auto encoded =
      backup_restore_recovery_presentation_internal::Encode(*presentation);
  if (!encoded) {
    return base::unexpected(encoded.error());
  }
  auto registry = MutableRegistry(local_state);
  if (!registry) {
    return base::unexpected(registry.error());
  }
  base::DictValue* entry = registry->FindDict(reservation_id);
  if (!entry || entry->FindInt(kVersionKey) != kLegacyRegistryEntryVersion ||
      entry->Find(kBackupRestoreRecoveryJournalKey) ||
      entry->Find(kRecoveryPresentationKey)) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  entry->Set(kVersionKey, kRegistryEntryVersion);
  entry->Set(kRecoveryPresentationKey, std::move(*encoded));
  local_state->SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      std::move(*registry));
  return presentation;
}

BackupRestoreRecoveryPresentationResult ReadBackupRestoreRecoveryPresentation(
    const PrefService* local_state,
    const std::string& reservation_id) {
  auto reservation = FindReservation(local_state, reservation_id);
  if (!reservation) {
    return base::unexpected(reservation.error());
  }
  const base::DictValue* entry =
      local_state
          ->GetDict(application_preferences::kBackupRestoreProfileReservations)
          .FindDict(reservation_id);
  const base::Value* encoded =
      entry ? entry->Find(kRecoveryPresentationKey) : nullptr;
  if (!encoded) {
    return base::unexpected(Error::kNotFound);
  }
  return backup_restore_recovery_presentation_internal::Decode(*encoded);
}

}  // namespace taffy
