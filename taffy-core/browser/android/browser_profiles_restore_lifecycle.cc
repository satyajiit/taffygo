// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_profile_registry.h"

namespace taffy {
namespace {

using Error = BackupRestoreProfileReserveError;
using Quarantine = BackupRestoreProfileQuarantineStatus;

constexpr size_t kMaximumRegularProfiles = 8u;
constexpr size_t kMaximumProfileNameLength = 40u;

bool IsVisibleRegularEntry(ProfileManager& manager,
                           ProfileAttributesEntry& entry) {
  const base::FilePath path = entry.GetPath();
  return !path.empty() && path != ProfileManager::GetGuestProfilePath() &&
         !entry.IsOmitted() && !entry.IsEphemeral() &&
         !IsProfileDirectoryMarkedForDeletion(path) &&
         manager.IsAllowedProfilePath(path);
}

std::u16string ValidatedDisplayName(std::u16string display_name) {
  base::TrimWhitespace(display_name, base::TRIM_ALL, &display_name);
  if (display_name.empty() || display_name.size() > kMaximumProfileNameLength ||
      std::ranges::any_of(display_name, [](char16_t character) {
        return character < 0x20 || character == 0x7f;
      })) {
    return {};
  }
  return display_name;
}

}  // namespace

void BrowserProfilesRestoreLifecycle::Reserve(Profile* source_profile,
                                              std::u16string display_name,
                                              ReserveCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (operation_in_flight()) {
    std::move(callback).Run(base::unexpected(Error::kBusy));
    return;
  }
  display_name = ValidatedDisplayName(std::move(display_name));
  if (!profile_manager_ || !local_state_ || !source_profile ||
      display_name.empty() || source_profile->IsOffTheRecord() ||
      !profile_manager_->IsValidProfile(source_profile) ||
      !profile_manager_->IsAllowedProfilePath(source_profile->GetPath()) ||
      profile_manager_->GetLastUsedProfileDir() != source_profile->GetPath()) {
    std::move(callback).Run(base::unexpected(Error::kInvalidArgument));
    return;
  }
  if (BackupRestoreQuarantineForProfilePath(local_state_,
                                            source_profile->GetPath()) !=
      Quarantine::kNotQuarantined) {
    std::move(callback).Run(base::unexpected(Error::kUnavailable));
    return;
  }
  BackupRestoreProfileReservationList reservations =
      ReadBackupRestoreProfileReservations(local_state_);
  if (!reservations.has_value()) {
    std::move(callback).Run(base::unexpected(Error::kRegistryRefused));
    return;
  }
  if (!reservations->empty()) {
    std::move(callback).Run(base::unexpected(Error::kBusy));
    return;
  }

  size_t regular_profile_count = 0u;
  for (ProfileAttributesEntry* entry :
       profile_manager_->GetProfileAttributesStorage()
           .GetAllProfilesAttributes()) {
    if (!entry || !IsVisibleRegularEntry(*profile_manager_, *entry)) {
      continue;
    }
    ++regular_profile_count;
    if (entry->GetLocalProfileName() == display_name) {
      std::move(callback).Run(base::unexpected(Error::kDuplicateName));
      return;
    }
  }
  if (regular_profile_count >= kMaximumRegularProfiles) {
    std::move(callback).Run(base::unexpected(Error::kLimitReached));
    return;
  }

  const base::FilePath target_profile_path =
      profile_manager_->GetNextExpectedProfileDirectoryPath();
  if (target_profile_path.empty() ||
      !profile_manager_->IsAllowedProfilePath(target_profile_path) ||
      target_profile_path == source_profile->GetPath() ||
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_profile_path) ||
      profile_manager_->GetProfileByPath(target_profile_path)) {
    std::move(callback).Run(base::unexpected(Error::kUnavailable));
    return;
  }

  pending_ = std::make_unique<PendingReservation>();
  pending_->source_profile = source_profile;
  pending_->display_name = std::move(display_name);
  pending_->reservation_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  pending_->target_profile_path = target_profile_path;
  pending_->callback = std::move(callback);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&base::PathExists, target_profile_path),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnInitialPathCheckComplete,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnInitialPathCheckComplete(
    bool path_exists) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  if (path_exists) {
    Finish(base::unexpected(Error::kTargetPathOccupied));
    return;
  }
  if (!profile_manager_->IsValidProfile(pending_->source_profile) ||
      profile_manager_->GetNextExpectedProfileDirectoryPath() !=
          pending_->target_profile_path ||
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(pending_->target_profile_path) ||
      profile_manager_->GetProfileByPath(pending_->target_profile_path)) {
    Finish(base::unexpected(Error::kBusy));
    return;
  }
  const base::FilePath generated =
      profile_manager_->GenerateNextProfileDirectoryPath();
  if (generated != pending_->target_profile_path) {
    Finish(base::unexpected(Error::kBusy));
    return;
  }
  auto reserved = ReserveBackupRestoreProfilePath(
      local_state_, pending_->reservation_id,
      pending_->source_profile->GetPath().BaseName(),
      pending_->target_profile_path.BaseName());
  if (!reserved.has_value()) {
    Finish(base::unexpected(Error::kRegistryRefused));
    return;
  }
  // Drain the write queue, then independently read back the exact persisted
  // quarantine and generated-path counter. JsonPrefStore's callback alone
  // does not report whether the disk replacement succeeded.
  local_state_->CommitPendingWrite(
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnQuarantineWriteDrained,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::Finish(ReserveResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  const bool cancellation_requested =
      static_cast<bool>(pending_pretransfer_cleanup_callback_);
  if (result.has_value()) {
    owned_precommit_reservation_ =
        std::make_unique<OwnedPrecommitBackupRestoreReservation>();
    owned_precommit_reservation_->reservation_id = pending_->reservation_id;
    owned_precommit_reservation_->source_profile_path =
        pending_->source_profile->GetPath();
    owned_precommit_reservation_->target_profile_path =
        pending_->target_profile_path;
  } else if (cancellation_requested) {
    auto reservations = ReadBackupRestoreProfileReservations(local_state_);
    if (reservations && reservations->size() == 1u &&
        reservations->front().reservation_id == pending_->reservation_id &&
        reservations->front().source_profile_base_name ==
            pending_->source_profile->GetPath().BaseName() &&
        reservations->front().target_profile_base_name ==
            pending_->target_profile_path.BaseName()) {
      owned_precommit_reservation_ =
          std::make_unique<OwnedPrecommitBackupRestoreReservation>();
      owned_precommit_reservation_->reservation_id = pending_->reservation_id;
      owned_precommit_reservation_->source_profile_path =
          pending_->source_profile->GetPath();
      owned_precommit_reservation_->target_profile_path =
          pending_->target_profile_path;
      owned_precommit_reservation_->target_profile_id =
          reservations->front().target_profile_id;
    }
  }
  ReserveCallback callback = std::move(pending_->callback);
  pending_.reset();
  base::WeakPtr<BrowserProfilesRestoreLifecycle> weak_this =
      weak_factory_.GetWeakPtr();
  std::move(callback).Run(
      cancellation_requested
          ? ReserveResult(base::unexpected(Error::kCancelled))
          : std::move(result));
  if (weak_this && cancellation_requested) {
    weak_this->BeginOwnedPrecommitCleanup();
  }
}

}  // namespace taffy
