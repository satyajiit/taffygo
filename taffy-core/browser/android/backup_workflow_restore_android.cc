// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/backup_workflow_restore_android.h"

#include <limits>
#include <utility>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/functional/bind.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/backup_workflow_android_notifications.h"
#include "taffy/browser/android/backup_workflow_restore_android_internal.h"

namespace taffy {
namespace {

constexpr size_t kMaximumRestoreOperations = 4u;
constexpr size_t kFieldsPerClass = 7u;

std::optional<std::vector<int32_t>> FlattenSummary(
    const ProfileBackupWorkflow::RestorePreparation& result) {
  if (result.status !=
          ProfileBackupWorkflow::RestorePreparationStatus::kReady ||
      result.review_token == 0u || result.target_profile_label.empty() ||
      result.selected_classes.empty() || result.selected_classes.size() > 6u) {
    return std::nullopt;
  }
  std::vector<int32_t> flattened;
  flattened.reserve(result.selected_classes.size() * kFieldsPerClass);
  for (const auto& row : result.selected_classes) {
    flattened.push_back(static_cast<int32_t>(row.kind));
    for (uint32_t count : row.action_counts) {
      if (count > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return std::nullopt;
      }
      flattened.push_back(static_cast<int32_t>(count));
    }
  }
  return flattened;
}

void DeliverPreparation(jni_zero::ScopedJavaGlobalRef<jobject> caller,
                        const std::string& operation_id,
                        ProfileBackupWorkflow::RestorePreparation result) {
  if (!caller) {
    return;
  }
  auto flattened = FlattenSummary(result);
  if (!flattened) {
    result.status =
        result.status ==
                ProfileBackupWorkflow::RestorePreparationStatus::kRefused
            ? ProfileBackupWorkflow::RestorePreparationStatus::kRefused
            : ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable;
    result.review_token = 0u;
    result.target_profile_label.clear();
    flattened.emplace();
    result.has_conflicts = false;
    result.can_stage = false;
  }
  backup_workflow_notifications::RestorePrepared(
      caller, operation_id, static_cast<int64_t>(result.review_token),
      result.target_profile_label, *flattened, result.has_conflicts,
      result.can_stage, static_cast<int32_t>(result.status));
}

}  // namespace

BackupWorkflowRestoreAndroid::Operation::Operation(
    ProfileBackupWorkflow::WindowToken owner,
    std::string operation_id,
    std::u16string target_profile_label,
    ProfileManager* profile_manager,
    PrefService* local_state)
    : owner(owner),
      operation_id(std::move(operation_id)),
      target_profile_label(std::move(target_profile_label)),
      creation_lifecycle(
          std::make_unique<BrowserProfilesRestoreLifecycle>(profile_manager,
                                                            local_state)) {}

BackupWorkflowRestoreAndroid::Operation::~Operation() = default;

BackupWorkflowRestoreAndroid::BackupWorkflowRestoreAndroid(
    Profile* source_profile,
    ProfileManager* profile_manager,
    PrefService* local_state,
    ProfileBackupWorkflow* workflow)
    : source_profile_(source_profile),
      profile_manager_(profile_manager),
      local_state_(local_state),
      workflow_(workflow) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

BackupWorkflowRestoreAndroid::~BackupWorkflowRestoreAndroid() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

bool BackupWorkflowRestoreAndroid::Prepare(
    const jni_zero::JavaRef<jobject>& caller,
    ProfileBackupWorkflow::WindowToken window,
    std::string operation_id,
    std::u16string target_profile_label) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!caller || !source_profile_ || !profile_manager_ || !local_state_ ||
      !workflow_ ||
      operations_.size() + pending_recovery_discoveries_.size() +
              recovered_reviews_.size() >=
          kMaximumRestoreOperations ||
      operations_.contains(operation_id)) {
    return false;
  }
  auto operation =
      std::make_unique<Operation>(window, operation_id, target_profile_label,
                                  profile_manager_, local_state_);
  operation->prepare_caller.Reset(caller);
  if (!workflow_->BeginImportedRestorePreparation(
          window, operation_id, target_profile_label,
          base::BindOnce(&BackupWorkflowRestoreAndroid::OnWorkflowPrepared,
                         weak_factory_.GetWeakPtr(), operation_id))) {
    return false;
  }
  Operation* const owned = operation.get();
  operations_.emplace(operation_id, std::move(operation));
  owned->creation_lifecycle->Reserve(
      source_profile_, owned->target_profile_label,
      base::BindOnce(&BackupWorkflowRestoreAndroid::OnReserved,
                     weak_factory_.GetWeakPtr(), operation_id));
  return true;
}

void BackupWorkflowRestoreAndroid::OnReserved(
    const std::string& operation_id,
    BrowserProfilesRestoreLifecycle::ReserveResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() ||
      found->second->phase != Operation::Phase::kReserving) {
    return;
  }
  if (!result) {
    FailPreparationAndClean(
        operation_id,
        result.error() == BackupRestoreProfileReserveError::kInvalidArgument ||
                result.error() ==
                    BackupRestoreProfileReserveError::kLimitReached ||
                result.error() ==
                    BackupRestoreProfileReserveError::kDuplicateName
            ? ProfileBackupWorkflow::RestorePreparationStatus::kRefused
            : ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable);
    return;
  }
  Operation& operation = *found->second;
  operation.reservation_id = result->reservation_id;
  const base::Uuid target_id = base::Uuid::GenerateRandomV4();
  if (!target_id.is_valid()) {
    FailPreparationAndClean(
        operation_id,
        ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable);
    return;
  }
  operation.target_profile_id = target_id.AsLowercaseString();
  operation.phase = Operation::Phase::kBinding;
  operation.creation_lifecycle->BindTargetProfileId(
      operation.reservation_id, operation.target_profile_id,
      base::BindOnce(&BackupWorkflowRestoreAndroid::OnTargetBound,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void BackupWorkflowRestoreAndroid::OnTargetBound(
    const std::string& operation_id,
    BrowserProfilesRestoreLifecycle::BindResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() ||
      found->second->phase != Operation::Phase::kBinding) {
    return;
  }
  if (!result) {
    FailPreparationAndClean(
        operation_id,
        ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable);
    return;
  }
  found->second->phase = Operation::Phase::kInitializing;
  found->second->creation_lifecycle->InitializeDormantTarget(
      std::move(*result),
      base::BindOnce(&BackupWorkflowRestoreAndroid::OnTargetInitialized,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void BackupWorkflowRestoreAndroid::OnTargetInitialized(
    const std::string& operation_id,
    BrowserProfilesRestoreLifecycle::DormantTargetResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() ||
      found->second->phase != Operation::Phase::kInitializing) {
    return;
  }
  if (!result) {
    FailPreparationAndClean(
        operation_id,
        ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable);
    return;
  }
  Operation& operation = *found->second;
  operation.cleanup_handle.emplace(std::move(result->cleanup));
  operation.phase = Operation::Phase::kPlanning;
  // Publish the handoff before calling into the workflow. Core readiness and
  // target cleanup callbacks may both run synchronously, and a Java callback
  // may close the window before AttachImportedRestoreTarget returns. From
  // that point onward only the coordinator/receipt path owns cleanup.
  operation.target_adopted = true;
  base::WeakPtr<BackupWorkflowRestoreAndroid> weak_this =
      weak_factory_.GetWeakPtr();
  const bool adopted = workflow_->AttachImportedRestoreTarget(
      operation.owner, operation_id, operation.reservation_id,
      std::move(result->target),
      base::BindOnce(&BackupWorkflowRestoreAndroid::PersistRecoveryPresentation,
                     weak_factory_.GetWeakPtr(), operation_id),
      base::BindOnce(&BackupWorkflowRestoreAndroid::OnPrecommitCancellation,
                     weak_factory_.GetWeakPtr(), operation_id));
  if (!weak_this) {
    return;
  }
  found = weak_this->operations_.find(operation_id);
  if (found == weak_this->operations_.end()) {
    return;
  }
  if (!adopted) {
    // A false return guarantees no workflow callback and no transferred
    // cleanup receipt. Reclaim the exact pre-transfer lifecycle immediately.
    found->second->target_adopted = false;
    weak_this->FailPreparationAndClean(
        operation_id,
        ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable);
  }
}

void BackupWorkflowRestoreAndroid::PersistRecoveryPresentation(
    const std::string& operation_id,
    std::string reservation_id,
    std::u16string target_profile_label,
    std::vector<core_service::mojom::BackupRecordKind> original_selection,
    core_service::mojom::BackupRestorePlanResultPtr exact_plan,
    ProfileBackupWorkflow::RestorePresentationCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (!callback) {
    return;
  }
  if (found == operations_.end() ||
      found->second->phase != Operation::Phase::kPlanning ||
      !found->second->target_adopted ||
      found->second->reservation_id != reservation_id ||
      found->second->target_profile_label != target_profile_label ||
      !found->second->creation_lifecycle || !exact_plan ||
      !exact_plan->target ||
      exact_plan->target->profile_id != found->second->target_profile_id) {
    std::move(callback).Run(base::unexpected(
        BackupRestoreProfileRegistryError::kWrongPhysicalState));
    return;
  }
  found->second->phase = Operation::Phase::kPresentationPersisting;
  found->second->creation_lifecycle->PersistRecoveryPresentation(
      std::move(reservation_id), std::move(target_profile_label),
      std::move(original_selection), std::move(exact_plan),
      base::BindOnce(
          &BackupWorkflowRestoreAndroid::OnRecoveryPresentationPersisted,
          weak_factory_.GetWeakPtr(), operation_id, std::move(callback)));
}

void BackupWorkflowRestoreAndroid::OnRecoveryPresentationPersisted(
    const std::string& operation_id,
    ProfileBackupWorkflow::RestorePresentationCallback callback,
    BackupRestoreRecoveryPresentationResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() ||
      found->second->phase != Operation::Phase::kPresentationPersisting) {
    std::move(callback).Run(base::unexpected(
        BackupRestoreProfileRegistryError::kWrongPhysicalState));
    return;
  }
  found->second->phase = Operation::Phase::kPlanning;
  std::move(callback).Run(std::move(result));
}

void BackupWorkflowRestoreAndroid::OnWorkflowPrepared(
    const std::string& operation_id,
    ProfileBackupWorkflow::RestorePreparation result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end()) {
    return;
  }
  Operation& operation = *found->second;
  auto caller = std::move(operation.prepare_caller);
  if (result.status ==
      ProfileBackupWorkflow::RestorePreparationStatus::kReady) {
    operation.review_token = result.review_token;
    operation.phase = Operation::Phase::kPreview;
  } else if (operation.target_adopted) {
    operation.phase = Operation::Phase::kCleaning;
  } else {
    operation.phase = Operation::Phase::kRecoveryRequired;
  }
  DeliverPreparation(std::move(caller), operation_id, std::move(result));
}

void BackupWorkflowRestoreAndroid::FailPreparationAndClean(
    const std::string& operation_id,
    ProfileBackupWorkflow::RestorePreparationStatus status) {
  auto found = operations_.find(operation_id);
  if (found == operations_.end()) {
    return;
  }
  Operation& operation = *found->second;
  operation.phase = Operation::Phase::kCleaning;
  base::WeakPtr<BackupWorkflowRestoreAndroid> weak_this =
      weak_factory_.GetWeakPtr();
  workflow_->FailImportedRestorePreparation(operation.owner, operation_id,
                                            status);
  if (weak_this && weak_this->operations_.contains(operation_id)) {
    weak_this->StartPretransferCleanup(operation_id);
  }
}

void BackupWorkflowRestoreAndroid::StartPretransferCleanup(
    const std::string& operation_id) {
  auto found = operations_.find(operation_id);
  if (found == operations_.end() || !found->second->creation_lifecycle ||
      found->second->target_adopted ||
      found->second->pretransfer_cleanup_started) {
    return;
  }
  found->second->pretransfer_cleanup_started = true;
  found->second->creation_lifecycle->CancelPendingNewReservation(
      base::BindOnce(&BackupWorkflowRestoreAndroid::OnPretransferCleanup,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void BackupWorkflowRestoreAndroid::OnPretransferCleanup(
    const std::string& operation_id,
    BrowserProfilesRestoreLifecycle::PrecommitCleanupResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end()) {
    return;
  }
  if (!result) {
    found->second->phase = Operation::Phase::kRecoveryRequired;
  }
  const ProfileBackupWorkflow::WindowToken owner = found->second->owner;
  workflow_->AbandonOperation(owner, operation_id);
  operations_.erase(found);
}

}  // namespace taffy
