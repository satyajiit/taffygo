// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy::restore_resolution_internal {
namespace {

namespace mojom = core_service::mojom;
using Error = BackupRestoreCandidateResolutionError;
using Quarantine = BackupRestoreProfileQuarantineStatus;
using Witness = BackupRestorePreferenceWriteWitness;

bool SameTarget(const ResolvedDormantBackupRestoreTarget& left,
                const ResolvedDormantBackupRestoreTarget& right) {
  return left == right;
}

}  // namespace

base::expected<ResolvedDormantBackupRestoreTarget, Error> ResolveTarget(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const std::string& reservation_id) {
  if (!profile_manager || !local_state || reservation_id.empty() ||
      profile_manager->user_data_dir().empty() ||
      !profile_manager->user_data_dir().IsAbsolute()) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  if (!reservations || reservations->size() != 1u ||
      reservations->front().reservation_id != reservation_id ||
      reservations->front().physical_state !=
          BackupRestoreProfilePhysicalState::kProfileCreated ||
      !reservations->front().target_profile_id) {
    return base::unexpected(Error::kRegistryRefused);
  }
  const auto& reservation = reservations->front();
  const base::FilePath source_path = profile_manager->user_data_dir().Append(
      reservation.source_profile_base_name);
  const base::FilePath target_path = profile_manager->user_data_dir().Append(
      reservation.target_profile_base_name);
  if (source_path == target_path ||
      source_path.DirName() != profile_manager->user_data_dir() ||
      target_path.DirName() != profile_manager->user_data_dir() ||
      !profile_manager->IsAllowedProfilePath(source_path) ||
      !profile_manager->IsAllowedProfilePath(target_path)) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  Profile* const source = profile_manager->GetProfileByPath(source_path);
  Profile* const target = profile_manager->GetProfileByPath(target_path);
  ProfileAttributesEntry* const source_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(source_path);
  ProfileAttributesEntry* const target_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_path);
  if (!source || source->IsOffTheRecord() ||
      !profile_manager->IsValidProfile(source) || !source_entry ||
      source_entry->IsOmitted() || source_entry->IsEphemeral() ||
      profile_manager->GetLastUsedProfileDir() != source_path ||
      BackupRestoreQuarantineForProfilePath(local_state, source_path) !=
          Quarantine::kNotQuarantined ||
      BackupRestoreQuarantineForProfilePath(local_state, target_path) !=
          Quarantine::kQuarantined) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  if ((target &&
       (!target_entry || target == source || target->IsOffTheRecord() ||
        !profile_manager->IsValidProfile(target) ||
        CoreServiceManagerFactory::HasExistingInstanceForProfile(target))) ||
      (!target && target_entry && target_entry->GetPath() != target_path)) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  return ResolvedDormantBackupRestoreTarget{
      .reservation_id = reservation_id,
      .source_profile_path = source_path,
      .target_profile_path = target_path,
      .target_profile_id = *reservation.target_profile_id,
  };
}

base::expected<BackupRestoreProfileReservation, Error> ExactReservation(
    const PrefService* local_state,
    const ResolvedDormantBackupRestoreTarget& target) {
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  if (!reservations || reservations->size() != 1u) {
    return base::unexpected(Error::kRegistryRefused);
  }
  const auto& reservation = reservations->front();
  if (reservation.reservation_id != target.reservation_id ||
      reservation.source_profile_base_name !=
          target.source_profile_path.BaseName() ||
      reservation.target_profile_base_name !=
          target.target_profile_path.BaseName() ||
      reservation.physical_state !=
          BackupRestoreProfilePhysicalState::kProfileCreated ||
      !reservation.target_profile_id ||
      *reservation.target_profile_id != target.target_profile_id) {
    return base::unexpected(Error::kRegistryRefused);
  }
  return reservation;
}

bool ExactCurrentState(ProfileManager* profile_manager,
                       PrefService* local_state,
                       const ResolvedDormantBackupRestoreTarget& target,
                       const BackupRestoreProfileReservation& reservation,
                       const BackupRestoreRecoveryRecords& records) {
  auto current =
      ResolveTarget(profile_manager, local_state, target.reservation_id);
  auto current_reservation = ExactReservation(local_state, target);
  auto current_records =
      ReadBackupRestoreRecoveryJournal(local_state, target.reservation_id);
  return current && SameTarget(*current, target) && current_reservation &&
         *current_reservation == reservation && current_records &&
         restore_reconciliation_internal::ExactHistory(*current_records,
                                                       records) &&
         restore_reconciliation_internal::LiveSourceManager(
             profile_manager, target, *current_records);
}

mojom::BackupRestoreCandidateWitnessPtr WitnessFromHistory(
    const BackupRestoreRecoveryRecords& records) {
  if (records.empty() || !records.front() || !records.front()->binding) {
    return nullptr;
  }
  const auto& binding = records.front()->binding;
  auto witness = mojom::BackupRestoreCandidateWitness::New();
  witness->selection = binding->selection;
  witness->record_count = binding->record_count;
  witness->candidate_records_sha256 = binding->candidate_records_sha256;
  return witness;
}

mojom::OperationEnvelopePtr NewOperation(uint64_t generation,
                                         std::string_view kind) {
  constexpr uint64_t kDeadlineMillis = 30'000u;
  const uint64_t now = BackupPlanningNowMonotonicMillis();
  const base::Uuid operation_id = base::Uuid::GenerateRandomV4();
  const base::Uuid idempotency_key = base::Uuid::GenerateRandomV4();
  if (generation == 0u || kind.empty() || kind.size() > 32u ||
      !operation_id.is_valid() || !idempotency_key.is_valid() ||
      now > std::numeric_limits<uint64_t>::max() - kDeadlineMillis) {
    return nullptr;
  }
  auto operation = mojom::OperationEnvelope::New(
      base::StrCat(
          {"backup-recovery-", kind, "-", operation_id.AsLowercaseString()}),
      generation, 0u, now + kDeadlineMillis,
      base::StrCat({"backup-recovery-", kind, "-",
                    idempotency_key.AsLowercaseString()}));
  return IsLiveBackupOperation(operation.get(), generation, now)
             ? std::move(operation)
             : nullptr;
}

std::vector<Witness> CaptureResolutionWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path,
    bool require_target_attributes) {
  const std::string target_key = target_profile_path.BaseName().AsUTF8Unsafe();
  const base::Value* target_attributes =
      local_state.GetDict(prefs::kProfileAttributes).Find(target_key);
  if (require_target_attributes && !target_attributes) {
    return {};
  }
  std::vector<Witness> witnesses;
  witnesses.push_back(
      {.dotted_path =
           application_preferences::kBackupRestoreProfileReservations,
       .expected = base::Value(
           local_state
               .GetDict(
                   application_preferences::kBackupRestoreProfileReservations)
               .Clone())});
  Witness attributes{.dotted_path = base::StrCat(
                         {prefs::kProfileAttributes, ".", target_key})};
  if (target_attributes) {
    attributes.expected = target_attributes->Clone();
  } else {
    attributes.must_be_absent = true;
  }
  witnesses.push_back(std::move(attributes));
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesOrder,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesOrder).Clone())});
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesDeleted,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesDeleted).Clone())});
  return witnesses;
}

void VerifyResolutionPreferences(ProfileManager* profile_manager,
                                 std::vector<Witness> witnesses,
                                 base::OnceCallback<void(bool)> callback) {
  if (!profile_manager || witnesses.empty() || !callback) {
    if (callback) {
      std::move(callback).Run(false);
    }
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          &BackupRestorePreferenceFileMatchesAndSync,
          profile_manager->user_data_dir().Append(chrome::kLocalStateFilename),
          std::move(witnesses)),
      std::move(callback));
}

}  // namespace taffy::restore_resolution_internal
