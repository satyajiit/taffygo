// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_ANDROID_H_
#define TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_ANDROID_H_

#include <jni.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/android/scoped_java_ref.h"
#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/profile_backup_workflow.h"

class PrefService;
class Profile;
class ProfileManager;

namespace taffy {

class BackupWorkflowRestoreAndroidTestPeer;

// Android-only physical orchestration behind one ProfileBackupWorkflow. It
// retains profile/reservation custody; Java sees only opaque live/recovered
// review tokens and bounded summaries. No method starts a target Core or
// activates a target.
class BackupWorkflowRestoreAndroid {
 public:
  BackupWorkflowRestoreAndroid(Profile* source_profile,
                               ProfileManager* profile_manager,
                               PrefService* local_state,
                               ProfileBackupWorkflow* workflow);
  BackupWorkflowRestoreAndroid(const BackupWorkflowRestoreAndroid&) = delete;
  BackupWorkflowRestoreAndroid& operator=(const BackupWorkflowRestoreAndroid&) =
      delete;
  ~BackupWorkflowRestoreAndroid();

  bool Prepare(const jni_zero::JavaRef<jobject>& caller,
               ProfileBackupWorkflow::WindowToken window,
               std::string operation_id,
               std::u16string target_profile_label);
  bool ConfirmAndStage(const jni_zero::JavaRef<jobject>& caller,
                       ProfileBackupWorkflow::WindowToken window,
                       ProfileBackupWorkflow::RestoreReviewToken review_token);
  bool Commit(const jni_zero::JavaRef<jobject>& caller,
              ProfileBackupWorkflow::WindowToken window,
              ProfileBackupWorkflow::RestoreReviewToken review_token);
  bool Resolve(const jni_zero::JavaRef<jobject>& caller,
               ProfileBackupWorkflow::WindowToken window,
               ProfileBackupWorkflow::RestoreReviewToken review_token,
               core_service::mojom::BackupRestoreResolutionChoice choice);

  // Restart discovery is observational and lives outside ProfileBackupWorkflow:
  // it reconstructs neither a key session nor a prior Core operation. A
  // delivered recovered-review token is fresh, window-local presentation
  // custody whose resolution still requires new source-Core authority.
  bool DiscoverInterruptedRestore(const jni_zero::JavaRef<jobject>& caller,
                                  ProfileBackupWorkflow::WindowToken window,
                                  uint64_t request_token);
  void AbandonInterruptedRestoreDiscovery(
      ProfileBackupWorkflow::WindowToken window,
      uint64_t request_token);
  bool ResolveRecoveredRestore(
      const jni_zero::JavaRef<jobject>& caller,
      ProfileBackupWorkflow::WindowToken window,
      uint64_t recovered_review_token,
      core_service::mojom::BackupRestoreResolutionChoice choice);
  void AbandonRecoveredRestoreReview(ProfileBackupWorkflow::WindowToken window,
                                     uint64_t recovered_review_token);

  void OnOperationAbandoned(ProfileBackupWorkflow::WindowToken window,
                            const std::string& operation_id);
  void OnWindowUnregistered(ProfileBackupWorkflow::WindowToken window);

 private:
  friend class BackupWorkflowRestoreAndroidTestPeer;

  struct Operation;
  struct PendingRecoveryDiscovery;
  struct RecoveredReview;

  void OnReserved(const std::string& operation_id,
                  BrowserProfilesRestoreLifecycle::ReserveResult result);
  void OnTargetBound(const std::string& operation_id,
                     BrowserProfilesRestoreLifecycle::BindResult result);
  void OnTargetInitialized(
      const std::string& operation_id,
      BrowserProfilesRestoreLifecycle::DormantTargetResult result);
  void PersistRecoveryPresentation(
      const std::string& operation_id,
      std::string reservation_id,
      std::u16string target_profile_label,
      std::vector<core_service::mojom::BackupRecordKind> original_selection,
      core_service::mojom::BackupRestorePlanResultPtr exact_plan,
      ProfileBackupWorkflow::RestorePresentationCallback callback);
  void OnRecoveryPresentationPersisted(
      const std::string& operation_id,
      ProfileBackupWorkflow::RestorePresentationCallback callback,
      BackupRestoreRecoveryPresentationResult result);
  void OnWorkflowPrepared(const std::string& operation_id,
                          ProfileBackupWorkflow::RestorePreparation result);
  void OnPrecommitCancellation(
      const std::string& operation_id,
      ProfileBackupCoordinator::PrecommitCancellationResult result);
  void OnPrecommitCleanup(
      const std::string& operation_id,
      BrowserProfilesRestoreLifecycle::PrecommitCleanupResult result);
  void OnPretransferCleanup(
      const std::string& operation_id,
      BrowserProfilesRestoreLifecycle::PrecommitCleanupResult result);
  void OnWorkflowStaged(const std::string& operation_id,
                        ProfileBackupWorkflow::RestoreStageStatus status);
  void OnWorkflowCommitted(const std::string& operation_id,
                           ProfileBackupWorkflow::RestoreCommitStatus status);
  void OnCandidateResolved(
      ProfileBackupWorkflow::RestoreReviewToken review_token,
      BrowserProfilesRestoreLifecycle::CandidateResolutionResult result);
  void OnWorkflowResolution(
      const std::string& operation_id,
      ProfileBackupWorkflow::RestoreResolutionStatus status);
  void OnInterruptedRestoreDiscovered(
      ProfileBackupWorkflow::WindowToken window,
      uint64_t request_token,
      BackupRestoreRestartDiscoveryResult result);
  void OnRecoveredCandidateResolved(
      uint64_t recovered_review_token,
      BrowserProfilesRestoreLifecycle::CandidateResolutionResult result);

  void FailPreparationAndClean(
      const std::string& operation_id,
      ProfileBackupWorkflow::RestorePreparationStatus status);
  void StartPretransferCleanup(const std::string& operation_id);
  Operation* FindByReview(ProfileBackupWorkflow::WindowToken window,
                          ProfileBackupWorkflow::RestoreReviewToken token);
  RecoveredReview* FindRecoveredReview(
      ProfileBackupWorkflow::WindowToken window,
      uint64_t token);
  uint64_t MintRecoveredReviewToken();
  void WithdrawRecoveredWindow(ProfileBackupWorkflow::WindowToken window);

  const raw_ptr<Profile> source_profile_;
  const raw_ptr<ProfileManager> profile_manager_;
  const raw_ptr<PrefService> local_state_;
  const raw_ptr<ProfileBackupWorkflow> workflow_;
  base::flat_map<std::string, std::unique_ptr<Operation>> operations_;
  base::flat_map<std::pair<ProfileBackupWorkflow::WindowToken, uint64_t>,
                 std::unique_ptr<PendingRecoveryDiscovery>>
      pending_recovery_discoveries_;
  base::flat_map<uint64_t, std::unique_ptr<RecoveredReview>> recovered_reviews_;
  uint64_t next_recovered_review_token_ =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  base::WeakPtrFactory<BackupWorkflowRestoreAndroid> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_ANDROID_H_
