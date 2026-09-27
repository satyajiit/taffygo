// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_RESTORE_TARGET_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_RESTORE_TARGET_H_

#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

enum class ProfileBackupRestoreTargetError {
  kInvalidAuthorization,
  kUnavailable,
  kPayloadMismatch,
  kStorageUnavailable,
  kBusy,
};

// Exact typed procedure records read back from the isolated verified stage.
// They are not an installed catalogue: the source Core must admit every
// definition against the retained plan before granting commit authority.
struct ProfileBackupRestoreStageObservation {
  std::vector<core_service::mojom::SkillRecordPtr> procedures;
};

// Physical evidence only. A known SQL result is not a durable recovery result
// until its exact observation has survived the reservation's write barrier.
// An unwitnessed outcome is always reported as unknown, never as permission
// to repeat the operation or delete the candidate.
struct ProfileBackupRestoreCommitObservation {
  core_service::mojom::BackupRestoreCommitOutcome outcome =
      core_service::mojom::BackupRestoreCommitOutcome::kOutcomeUnknown;
  bool journal_durable = false;
};

// Exclusive browser custody of one durably reserved, hidden dormant target.
// The production owner is minted by the profile lifecycle after synchronized
// identity binding and database initialization. Callers never choose its path,
// replace its identity, or start a target Core. Portable phase authority still
// comes only from the source Core; this owner consumes it for staging/commit.
//
// Calls and callbacks belong to the browser sequence. A callback may run
// inline and may destroy this owner; implementations must not access members
// after invoking it without a surviving weak pointer. Physical storage lives
// on the owner's blocking sequence. Abandon is ordered after pending staging,
// deletes only the exact isolated stage, and reports verified cleanup. A false
// cleanup result retains custody for retry. Destruction withdraws callbacks and
// must leave failed cleanup under the durable profile reservation, never
// publish or delete the target profile. Once commit is dispatched, Abandon
// cannot remove its stage: one atomic terminal drains under durable intent
// custody. Publication and target deletion remain separate operations.
class ProfileBackupRestoreTarget {
 public:
  using StageResult = base::expected<ProfileBackupRestoreStageObservation,
                                     ProfileBackupRestoreTargetError>;
  using StageCallback = base::OnceCallback<void(StageResult)>;
  using CommitResult = base::expected<ProfileBackupRestoreCommitObservation,
                                      ProfileBackupRestoreTargetError>;
  using CommitCallback = base::OnceCallback<void(CommitResult)>;
  using CleanupCallback = base::OnceCallback<void(bool)>;

  ProfileBackupRestoreTarget() = default;
  ProfileBackupRestoreTarget(const ProfileBackupRestoreTarget&) = delete;
  ProfileBackupRestoreTarget& operator=(const ProfileBackupRestoreTarget&) =
      delete;
  virtual ~ProfileBackupRestoreTarget() = default;

  virtual const std::string& target_profile_id() const = 0;
  virtual void Stage(
      core_service::mojom::BackupRestorePlanResultPtr plan,
      core_service::mojom::BackupRestoreStageAuthorizationPtr authorization,
      base::File plaintext_payload,
      StageCallback callback) = 0;
  // Validates and consumes one fresh source-Core authorization, verifies the
  // owned stage, and durably witnesses its exact content-free intent before
  // dispatching the typed SQL transaction. Final live source/custody checks
  // precede dispatch; after dispatch cancellation/source loss cannot retry or
  // interrupt that atomic terminal. The outcome is durably witnessed before
  // a definitive result is returned. Errors are pre-dispatch only and retain
  // custody; failure to witness a post-dispatch outcome returns unknown.
  virtual void Commit(
      core_service::mojom::BackupRestorePlanResultPtr plan,
      core_service::mojom::BackupRestoreCommitAuthorizationPtr authorization,
      CommitCallback callback) = 0;
  virtual void Abandon(CleanupCallback callback) = 0;
  // Permanently closes the mutable SQL owner after a consumed commit attempt
  // has drained, including an authority request whose reply was lost before
  // physical dispatch. It preserves the candidate, its isolated stage and
  // browser quarantine for restart reconciliation. A true callback runs only
  // after the blocking-sequence storage owner (and its exclusive path lease)
  // has been destroyed. This is a physical custody handoff, not authority to
  // inspect, publish or delete the candidate.
  virtual void CloseForRecovery(CleanupCallback callback) = 0;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_RESTORE_TARGET_H_
