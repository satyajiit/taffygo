// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_RESTORE_RESERVATION_RETIREMENT_H_
#define TAFFY_BROWSER_BACKUP_RESTORE_RESERVATION_RETIREMENT_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/token.h"
#include "taffy/browser/backup_restore_recovery_journal.h"

namespace taffy {

class BrowserProfilesRestoreLifecycle;

// Browser-sequence-only final bookkeeping. The physical owner MUST first
// synchronize publication/deletion and the exact Completed history, then
// obtain a fresh source-Core terminal classification of that same history.
// This owner authorizes no publication, deletion, or replay.
//
// Begin installs a process-local quarantine fence before clearing the exact
// reservation. It covers synchronous preference observers and the later disk
// barrier. Destruction without verified completion restores the prior registry
// before releasing that fence. Unexpected preference mutation/control retains
// the fence until restart rather than overwriting another value.
class BackupRestoreReservationRetirement {
 public:
  static base::expected<std::unique_ptr<BackupRestoreReservationRetirement>,
                        BackupRestoreProfileRegistryError>
  Begin(PrefService* local_state,
        const BackupRestoreProfileReservation& exact_reservation,
        const BackupRestoreRecoveryRecords& exact_terminal_history,
        const core_service::mojom::BackupRestoreRecoveryClassification&
            classification);

  BackupRestoreReservationRetirement(
      const BackupRestoreReservationRetirement&) = delete;
  BackupRestoreReservationRetirement& operator=(
      const BackupRestoreReservationRetirement&) = delete;
  ~BackupRestoreReservationRetirement();

  // Call only after exact preference absence and the publication/deletion
  // witnesses have synchronized. Failure leaves the fence owned. Success does
  // not activate a profile: selection remains a separate trusted UI action.
  bool ReleaseAfterVerifiedAbsence();

  static bool IsInProgress();
  static bool BlocksProfileBaseName(const base::FilePath& profile_base_name);

 private:
  friend class BrowserProfilesRestoreLifecycle;

  // The precommit path has no recovery journal to classify. Only the Android
  // lifecycle may call this after it has consumed either its still-local
  // creation custody or the coordinator's private cancellation receipt and
  // independently verified exact profile/cache absence.
  static base::expected<std::unique_ptr<BackupRestoreReservationRetirement>,
                        BackupRestoreProfileRegistryError>
  BeginPristinePrecommitCleanup(
      PrefService* local_state,
      const BackupRestoreProfileReservation& exact_reservation);

  BackupRestoreReservationRetirement(PrefService* local_state,
                                     base::FilePath target_profile_base_name,
                                     base::DictValue prior_registry);

  const raw_ptr<PrefService> local_state_;
  const base::Token fence_id_ = base::Token::CreateRandom();
  const base::DictValue prior_registry_;
  bool released_ = false;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_RESTORE_RESERVATION_RETIREMENT_H_
