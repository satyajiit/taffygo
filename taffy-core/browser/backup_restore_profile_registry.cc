// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_profile_registry.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/json/values_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_recovery_codec.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"
#include "taffy/browser/backup_restore_reservation_retirement.h"

namespace taffy {
namespace {

using Error = BackupRestoreProfileRegistryError;
using PhysicalState = BackupRestoreProfilePhysicalState;

constexpr int kLegacyRegistryEntryVersion = 1;
constexpr int kRegistryEntryVersion = 2;
constexpr char kVersionKey[] = "version";
constexpr char kSourceProfileKey[] = "source_profile";
constexpr char kTargetProfileKey[] = "target_profile";
constexpr char kPhysicalStateKey[] = "physical_state";
constexpr char kTargetProfileIdKey[] = "target_profile_id";
constexpr char kRecoveryPresentationKey[] = "recovery_presentation";
constexpr char kPathReservedState[] = "path-reserved";
constexpr char kProfileCreatedState[] = "profile-created";

bool IsGeneratedProfileBaseName(const base::FilePath& value) {
  // ProfileManager::GenerateNextProfileDirectoryPath() uses exactly
  // chrome::kMultiProfileDirPrefix ("Profile ") plus the current positive
  // integer counter. Repeating that grammar here is fail-closed: if Chromium
  // changes it, reservation stops instead of treating another user-data
  // sibling as disposable profile custody.
  if (value.empty() || value.IsAbsolute() || value.BaseName() != value) {
    return false;
  }
  const std::string encoded = value.AsUTF8Unsafe();
  constexpr std::string_view kPrefix = "Profile ";
  if (!encoded.starts_with(kPrefix)) {
    return false;
  }
  const std::string_view suffix =
      std::string_view(encoded).substr(kPrefix.size());
  int number = 0;
  return !suffix.empty() && suffix.size() <= 10u &&
         base::StringToInt(suffix, &number) && number > 0 &&
         base::NumberToString(number) == suffix;
}

bool IsRegularProfileBaseName(const base::FilePath& value) {
  return value == base::FilePath(FILE_PATH_LITERAL("Default")) ||
         IsGeneratedProfileBaseName(value);
}

bool IsCanonicalUuidV4(std::string_view value) {
  const base::Uuid parsed = base::Uuid::ParseLowercase(value);
  return parsed.is_valid() && parsed.AsLowercaseString() == value &&
         value[14] == '4' &&
         (value[19] == '8' || value[19] == '9' || value[19] == 'a' ||
          value[19] == 'b');
}

const PrefService::Preference* RegistryPreference(
    const PrefService* local_state) {
  if (!local_state) {
    return nullptr;
  }
  const PrefService::Preference* preference = local_state->FindPreference(
      application_preferences::kBackupRestoreProfileReservations);
  if (!preference || preference->GetType() != base::Value::Type::DICT ||
      !preference->GetValue()) {
    return nullptr;
  }
  return preference;
}

std::optional<PhysicalState> ParsePhysicalState(std::string_view value) {
  if (value == kPathReservedState) {
    return PhysicalState::kPathReserved;
  }
  if (value == kProfileCreatedState) {
    return PhysicalState::kProfileCreated;
  }
  return std::nullopt;
}

std::string_view PhysicalStateName(PhysicalState state) {
  switch (state) {
    case PhysicalState::kPathReserved:
      return kPathReservedState;
    case PhysicalState::kProfileCreated:
      return kProfileCreatedState;
  }
  return {};
}

base::expected<BackupRestoreProfileReservation, Error> ParseReservation(
    std::string_view reservation_id,
    const base::Value& value) {
  if (!IsCanonicalUuidV4(reservation_id) || !value.is_dict()) {
    return base::unexpected(Error::kCorrupt);
  }
  const base::DictValue& entry = value.GetDict();
  const std::optional<int> version = entry.FindInt(kVersionKey);
  const base::Value* source_value = entry.Find(kSourceProfileKey);
  const base::Value* target_value = entry.Find(kTargetProfileKey);
  const std::string* physical_state = entry.FindString(kPhysicalStateKey);
  const std::string* target_profile_id = entry.FindString(kTargetProfileIdKey);
  const std::optional<base::FilePath> source =
      base::ValueToFilePath(source_value);
  const std::optional<base::FilePath> target =
      base::ValueToFilePath(target_value);
  const std::optional<PhysicalState> parsed_state =
      physical_state ? ParsePhysicalState(*physical_state) : std::nullopt;
  const base::Value* recovery = entry.Find(kBackupRestoreRecoveryJournalKey);
  const base::Value* presentation = entry.Find(kRecoveryPresentationKey);
  const size_t expected_size = (target_profile_id ? 5u : 4u) +
                               (recovery ? 1u : 0u) + (presentation ? 1u : 0u);
  if (!version ||
      (*version != kLegacyRegistryEntryVersion &&
       *version != kRegistryEntryVersion) ||
      (*version == kLegacyRegistryEntryVersion && presentation) ||
      (*version == kRegistryEntryVersion && !presentation) || !source ||
      !target || !IsRegularProfileBaseName(*source) ||
      !IsGeneratedProfileBaseName(*target) || *source == *target ||
      !parsed_state || entry.size() != expected_size ||
      (target_profile_id && !IsCanonicalUuidV4(*target_profile_id)) ||
      (target_profile_id && *parsed_state != PhysicalState::kProfileCreated)) {
    return base::unexpected(Error::kCorrupt);
  }

  std::optional<BackupRestoreRecoveryPresentation> decoded_presentation;
  if (presentation) {
    auto decoded =
        backup_restore_recovery_presentation_internal::Decode(*presentation);
    if (!decoded || !target_profile_id ||
        *parsed_state != PhysicalState::kProfileCreated ||
        decoded->target_profile_id != *target_profile_id) {
      return base::unexpected(Error::kCorrupt);
    }
    decoded_presentation = std::move(*decoded);
  }

  BackupRestoreRecoveryRecords decoded_recovery;
  if (recovery) {
    if (!target_profile_id) {
      return base::unexpected(Error::kCorrupt);
    }
    auto decoded = DecodeBackupRestoreRecoveryJournal(*recovery, reservation_id,
                                                      *target_profile_id);
    if (!decoded) {
      return base::unexpected(Error::kCorrupt);
    }
    decoded_recovery = std::move(*decoded);
  }
  if (decoded_presentation && !decoded_recovery.empty() &&
      !backup_restore_recovery_presentation_internal::MatchesRecoveryBinding(
          *decoded_presentation, reservation_id,
          *decoded_recovery.front()->binding)) {
    return base::unexpected(Error::kCorrupt);
  }
  return BackupRestoreProfileReservation{
      .reservation_id = std::string(reservation_id),
      .source_profile_base_name = *source,
      .target_profile_base_name = *target,
      .physical_state = *parsed_state,
      .target_profile_id = target_profile_id
                               ? std::make_optional(*target_profile_id)
                               : std::nullopt,
  };
}

base::expected<base::DictValue, Error> MutableValidatedRegistry(
    PrefService* local_state) {
  const PrefService::Preference* preference = RegistryPreference(local_state);
  if (!preference || !preference->IsUserModifiable()) {
    return base::unexpected(Error::kUnavailable);
  }
  BackupRestoreProfileReservationList parsed =
      ReadBackupRestoreProfileReservations(local_state);
  if (!parsed.has_value()) {
    return base::unexpected(parsed.error());
  }
  return local_state
      ->GetDict(application_preferences::kBackupRestoreProfileReservations)
      .Clone();
}

base::expected<BackupRestoreProfileReservation, Error> FindReservation(
    const PrefService* local_state,
    const std::string& reservation_id) {
  BackupRestoreProfileReservationList reservations =
      ReadBackupRestoreProfileReservations(local_state);
  if (!reservations.has_value()) {
    return base::unexpected(reservations.error());
  }
  if (reservations->empty() ||
      reservations->front().reservation_id != reservation_id) {
    return base::unexpected(Error::kNotFound);
  }
  return reservations->front();
}

void StoreRegistry(PrefService* local_state, base::DictValue registry) {
  local_state->SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      std::move(registry));
}

}  // namespace

BackupRestoreProfileReservationList ReadBackupRestoreProfileReservations(
    const PrefService* local_state) {
  if (!RegistryPreference(local_state)) {
    return base::unexpected(Error::kUnavailable);
  }
  const PrefService::Preference* effective = RegistryPreference(local_state);
  // GetUserPrefValue() deliberately hits NOTREACHED when a raw user-store
  // value has the wrong registered type. HasUserSetting() observes the raw
  // presence without that assertion, while the effective typed read falls
  // back to this pref's empty default. An explicitly stored empty registry is
  // never valid (the no-reservation state is ClearPref/default), so the pair
  // distinguishes that rejected persisted value without triggering the
  // assertion. Higher-priority control also fails closed.
  const base::Value* effective_value = effective->GetValue();
  if ((effective->HasUserSetting() && !effective->IsUserControlled()) ||
      !effective_value->is_dict() ||
      (effective->HasUserSetting() && effective_value->GetDict().empty())) {
    return base::unexpected(Error::kCorrupt);
  }
  const base::DictValue& registry = local_state->GetDict(
      application_preferences::kBackupRestoreProfileReservations);
  if (registry.size() > 1u) {
    return base::unexpected(Error::kCorrupt);
  }
  std::vector<BackupRestoreProfileReservation> reservations;
  reservations.reserve(registry.size());
  for (const auto [reservation_id, value] : registry) {
    auto reservation = ParseReservation(reservation_id, value);
    if (!reservation.has_value()) {
      return base::unexpected(reservation.error());
    }
    reservations.push_back(std::move(*reservation));
  }
  return reservations;
}

base::expected<void, Error> ReserveBackupRestoreProfilePath(
    PrefService* local_state,
    std::string reservation_id,
    base::FilePath source_profile_base_name,
    base::FilePath target_profile_base_name) {
  if (!IsCanonicalUuidV4(reservation_id) ||
      !IsRegularProfileBaseName(source_profile_base_name) ||
      !IsGeneratedProfileBaseName(target_profile_base_name) ||
      source_profile_base_name == target_profile_base_name) {
    return base::unexpected(Error::kInvalidArgument);
  }
  if (BackupRestoreReservationRetirement::IsInProgress()) {
    return base::unexpected(Error::kBusy);
  }
  auto registry = MutableValidatedRegistry(local_state);
  if (!registry.has_value()) {
    return base::unexpected(registry.error());
  }
  if (!registry->empty()) {
    return base::unexpected(Error::kBusy);
  }
  base::DictValue entry;
  // A reservation remains in the legacy physical-only shape until a
  // successfully planned preview atomically attaches the v2 presentation.
  // This keeps every pre-plan cancellation checkpoint representable without
  // synthesizing UI facts that do not exist yet.
  entry.Set(kVersionKey, kLegacyRegistryEntryVersion);
  entry.Set(kSourceProfileKey, base::FilePathToValue(source_profile_base_name));
  entry.Set(kTargetProfileKey, base::FilePathToValue(target_profile_base_name));
  entry.Set(kPhysicalStateKey, PhysicalStateName(PhysicalState::kPathReserved));
  registry->Set(reservation_id, std::move(entry));
  StoreRegistry(local_state, std::move(*registry));
  return base::ok();
}

base::expected<void, Error> MarkBackupRestoreProfileCreated(
    PrefService* local_state,
    const std::string& reservation_id) {
  auto reservation = FindReservation(local_state, reservation_id);
  if (!reservation.has_value()) {
    return base::unexpected(reservation.error());
  }
  if (reservation->physical_state == PhysicalState::kProfileCreated) {
    return base::ok();
  }
  if (reservation->physical_state != PhysicalState::kPathReserved) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  auto registry = MutableValidatedRegistry(local_state);
  if (!registry.has_value()) {
    return base::unexpected(registry.error());
  }
  base::DictValue* entry = registry->FindDict(reservation_id);
  if (!entry) {
    return base::unexpected(Error::kNotFound);
  }
  entry->Set(kPhysicalStateKey,
             PhysicalStateName(PhysicalState::kProfileCreated));
  StoreRegistry(local_state, std::move(*registry));
  return base::ok();
}

base::expected<void, Error> BindBackupRestoreTargetProfileId(
    PrefService* local_state,
    const std::string& reservation_id,
    std::string target_profile_id) {
  if (!IsCanonicalUuidV4(target_profile_id)) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto reservation = FindReservation(local_state, reservation_id);
  if (!reservation.has_value()) {
    return base::unexpected(reservation.error());
  }
  if (reservation->physical_state != PhysicalState::kProfileCreated) {
    return base::unexpected(Error::kWrongPhysicalState);
  }
  if (reservation->target_profile_id) {
    if (*reservation->target_profile_id == target_profile_id) {
      return base::ok();
    }
    return base::unexpected(Error::kWrongPhysicalState);
  }
  auto registry = MutableValidatedRegistry(local_state);
  if (!registry.has_value()) {
    return base::unexpected(registry.error());
  }
  base::DictValue* entry = registry->FindDict(reservation_id);
  if (!entry) {
    return base::unexpected(Error::kNotFound);
  }
  entry->Set(kTargetProfileIdKey, std::move(target_profile_id));
  StoreRegistry(local_state, std::move(*registry));
  return base::ok();
}

BackupRestoreProfileQuarantineStatus BackupRestoreQuarantineForProfilePath(
    const PrefService* local_state,
    const base::FilePath& profile_path) {
  if (!RegistryPreference(local_state)) {
    return BackupRestoreProfileQuarantineStatus::kRegistryUnavailable;
  }
  BackupRestoreProfileReservationList reservations =
      ReadBackupRestoreProfileReservations(local_state);
  if (!reservations.has_value()) {
    return reservations.error() == Error::kUnavailable
               ? BackupRestoreProfileQuarantineStatus::kRegistryUnavailable
               : BackupRestoreProfileQuarantineStatus::kRegistryCorrupt;
  }
  if (profile_path.empty() || !profile_path.IsAbsolute() ||
      !IsRegularProfileBaseName(profile_path.BaseName())) {
    return BackupRestoreProfileQuarantineStatus::kInvalidProfilePath;
  }
  const base::FilePath base_name = profile_path.BaseName();
  if (BackupRestoreReservationRetirement::BlocksProfileBaseName(base_name)) {
    return BackupRestoreProfileQuarantineStatus::kQuarantined;
  }
  for (const auto& reservation : *reservations) {
    if (reservation.target_profile_base_name == base_name) {
      return BackupRestoreProfileQuarantineStatus::kQuarantined;
    }
  }
  return BackupRestoreProfileQuarantineStatus::kNotQuarantined;
}

}  // namespace taffy
