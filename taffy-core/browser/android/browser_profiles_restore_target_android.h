// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_TARGET_ANDROID_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_TARGET_ANDROID_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"

namespace taffy {

// The deep move-only owner returned by the Android profile lifecycle. Its
// public surface is deliberately limited to authorized staging, one durable
// physical commit, and verified precommit stage cleanup; the profile remains
// quarantined for the owner's lifetime.
class BrowserProfilesRestoreLifecycle::DormantTargetState final
    : public ProfileBackupRestoreTarget {
 public:
  using InitializationResult =
      base::expected<void, BackupRestoreDormantTargetError>;
  using InitializationCallback = base::OnceCallback<void(InitializationResult)>;

  DormantTargetState(ProfileManager* profile_manager,
                     PrefService* local_state,
                     ResolvedDormantBackupRestoreTarget resolved_target);
  DormantTargetState(const DormantTargetState&) = delete;
  DormantTargetState& operator=(const DormantTargetState&) = delete;
  ~DormantTargetState() override;

  void Initialize(InitializationCallback callback);

  const std::string& target_profile_id() const override;
  void Stage(
      core_service::mojom::BackupRestorePlanResultPtr plan,
      core_service::mojom::BackupRestoreStageAuthorizationPtr authorization,
      base::File plaintext_payload,
      StageCallback callback) override;
  void Commit(
      core_service::mojom::BackupRestorePlanResultPtr plan,
      core_service::mojom::BackupRestoreCommitAuthorizationPtr authorization,
      CommitCallback callback) override;
  void Abandon(CleanupCallback callback) override;
  void CloseForRecovery(CleanupCallback callback) override;

  // Lifecycle-only drain used before this owner has been transferred. It is
  // refused after any portable stage or commit authority was consumed.
  void CloseForPrecommitCleanup(CleanupCallback callback);

 private:
  struct StageExecutionResult {
    StageResult result;
    bool authorization_consumed = false;
  };

  using CommitPreparationResult =
      base::expected<core_service::mojom::BackupRestoreCandidateWitnessPtr,
                     ProfileBackupRestoreTargetError>;

  class BlockingOwner {
   public:
    BlockingOwner();
    BlockingOwner(const BlockingOwner&) = delete;
    BlockingOwner& operator=(const BlockingOwner&) = delete;
    ~BlockingOwner();

    InitializationResult Initialize(base::FilePath target_profile_path,
                                    std::string target_profile_id);
    StageExecutionResult Stage(
        core_service::mojom::BackupRestorePlanResultPtr plan,
        core_service::mojom::BackupRestoreStageAuthorizationPtr authorization,
        base::File plaintext_payload,
        std::array<uint8_t, 32> expected_snapshot_sha256,
        uint64_t expected_record_count);
    CommitPreparationResult PrepareCommitWitness();
    core_service::mojom::BackupRestoreCommitOutcome Commit(
        core_service::mojom::BackupRestorePlanResultPtr plan,
        core_service::mojom::BackupRestoreCommitAuthorizationPtr authorization,
        core_service::mojom::BackupRestoreCandidateWitnessPtr witness);
    bool Abandon();
    bool CloseForRecovery(bool retain_stage_for_recovery);

   private:
    class StorageOwner;
    std::unique_ptr<StorageOwner> storage_owner_;
  };

  struct PendingStage;
  struct PendingCommit;

  bool HasExactLiveCustody() const;
  bool HasLiveCommitAuthority(
      const core_service::mojom::BackupRestoreCommitAuthorization&
          authorization) const;
  void VerifyCustody(base::OnceCallback<void(bool)> callback);
  void OnStagePreValidation(bool matches);
  void OnStageExecuted(StageExecutionResult result);
  void OnStagePostValidation(bool matches);
  void FinishStage(StageResult result);
  void OnCommitPreValidation(bool matches);
  void OnCommitWitnessPrepared(CommitPreparationResult result);
  void OnCommitIntentWriteDrained();
  void OnCommitIntentReadBack(bool matches);
  void DispatchCommitOrRecordRefusal();
  void OnCommitExecuted(
      core_service::mojom::BackupRestoreCommitOutcome outcome);
  void PersistCommitOutcome(
      core_service::mojom::BackupRestoreCommitOutcome outcome);
  void OnCommitOutcomeWriteDrained();
  void OnCommitOutcomeReadBack(bool matches);
  void FinishCommit(CommitResult result);
  void BeginAbandon();
  void OnAbandonPreValidation(bool matches);
  void OnAbandonExecuted(bool cleaned);
  void OnAbandonPostValidation(bool matches);
  void OnAbandonStorageClosed(bool closed);
  void FinishAbandon(bool cleaned);
  void OnClosedForRecovery(bool closed);

  const raw_ptr<ProfileManager> profile_manager_;
  const raw_ptr<PrefService> local_state_;
  const ResolvedDormantBackupRestoreTarget resolved_target_;
  base::SequenceBound<BlockingOwner> blocking_owner_;
  std::unique_ptr<PendingStage> pending_stage_;
  std::unique_ptr<PendingCommit> pending_commit_;
  core_service::mojom::BackupRestorePlanResultPtr staged_plan_;
  CleanupCallback cleanup_callback_;
  CleanupCallback close_for_recovery_callback_;
  bool stage_authorization_consumed_ = false;
  bool commit_authorization_consumed_ = false;
  bool commit_storage_dispatched_ = false;
  bool abandon_requested_ = false;
  bool cleanup_in_flight_ = false;
  bool cleanup_storage_succeeded_ = false;
  bool closed_for_recovery_ = false;
  base::WeakPtrFactory<DormantTargetState> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_TARGET_ANDROID_H_
