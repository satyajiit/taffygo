// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/backup_workflow_android_notifications.h"
#include "taffy/browser/android/backup_workflow_restore_android.h"
#include "taffy/browser/android/backup_workflow_restore_android_internal.h"
#include "taffy/browser/android/backup_workflow_restore_projection.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using DiscoveryStatus = BackupRestoreRestartDiscoveryStatus;
using ResolutionStatus = ProfileBackupWorkflow::RestoreResolutionStatus;

constexpr size_t kMaximumRestoreOperations = 4u;
constexpr int32_t kDiscoveryUnavailable = 11;

static_assert(static_cast<int32_t>(DiscoveryStatus::kNone) == 0);
static_assert(static_cast<int32_t>(DiscoveryStatus::kSourceUnavailable) == 1);
static_assert(static_cast<int32_t>(DiscoveryStatus::kPrecommit) == 2);
static_assert(static_cast<int32_t>(DiscoveryStatus::kPresentationUnavailable) ==
              3);
static_assert(static_cast<int32_t>(DiscoveryStatus::kSchemaMismatch) == 4);
static_assert(static_cast<int32_t>(DiscoveryStatus::kOutcomeUnknown) == 5);
static_assert(static_cast<int32_t>(DiscoveryStatus::kCustodyAmbiguous) == 6);
static_assert(static_cast<int32_t>(DiscoveryStatus::kRollbackAvailable) == 7);
static_assert(static_cast<int32_t>(DiscoveryStatus::kCleanupRequired) == 8);
static_assert(static_cast<int32_t>(DiscoveryStatus::kPublished) == 9);
static_assert(static_cast<int32_t>(DiscoveryStatus::kVerifiedDeleted) == 10);

void RetireLifecycle(
    std::unique_ptr<BrowserProfilesRestoreLifecycle> lifecycle) {
  if (!lifecycle) {
    return;
  }
  // Completion runs from inside the lifecycle's own callback frame. A
  // non-nestable deletion avoids invalidating that frame; shutdown failure
  // deliberately leaks this inert, callback-free shell rather than deleting
  // it re-entrantly.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(lifecycle));
}

void DeliverDiscovery(jni_zero::ScopedJavaGlobalRef<jobject> caller,
                      uint64_t request_token,
                      uint64_t review_token,
                      const BackupRestoreRecoveryPresentation* presentation,
                      bool cleanup_only,
                      int32_t status) {
  if (!caller) {
    return;
  }
  std::vector<int32_t> flattened;
  if (presentation) {
    auto projection =
        backup_workflow_restore_internal::FlattenRecoveryPresentation(
            *presentation);
    if (projection) {
      flattened = std::move(*projection);
    }
  }
  backup_workflow_notifications::InterruptedRestoreDiscovered(
      caller, static_cast<int64_t>(request_token),
      static_cast<int64_t>(review_token),
      presentation ? presentation->target_profile_label : std::u16string(),
      flattened, presentation && presentation->has_conflicts,
      presentation && presentation->can_stage, cleanup_only, status);
}

void DeliverRecoveredResolution(jni_zero::ScopedJavaGlobalRef<jobject> caller,
                                uint64_t token,
                                ResolutionStatus status) {
  if (caller) {
    backup_workflow_notifications::RecoveredRestoreResolved(
        caller, static_cast<int64_t>(token), static_cast<int32_t>(status));
  }
}

}  // namespace

BackupWorkflowRestoreAndroid::PendingRecoveryDiscovery::
    PendingRecoveryDiscovery(ProfileBackupWorkflow::WindowToken owner,
                             uint64_t request_token,
                             ProfileManager* profile_manager,
                             PrefService* local_state)
    : owner(owner),
      request_token(request_token),
      lifecycle(
          std::make_unique<BrowserProfilesRestoreLifecycle>(profile_manager,
                                                            local_state)) {}

BackupWorkflowRestoreAndroid::PendingRecoveryDiscovery::
    ~PendingRecoveryDiscovery() = default;

BackupWorkflowRestoreAndroid::RecoveredReview::RecoveredReview(
    ProfileBackupWorkflow::WindowToken owner,
    uint64_t token,
    std::string reservation_id,
    bool discard_only)
    : owner(owner),
      token(token),
      reservation_id(std::move(reservation_id)),
      discard_only(discard_only) {}

BackupWorkflowRestoreAndroid::RecoveredReview::~RecoveredReview() = default;

uint64_t BackupWorkflowRestoreAndroid::MintRecoveredReviewToken() {
  while (next_recovered_review_token_ != 0u &&
         recovered_reviews_.contains(next_recovered_review_token_)) {
    --next_recovered_review_token_;
  }
  if (next_recovered_review_token_ == 0u) {
    return 0u;
  }
  return next_recovered_review_token_--;
}

BackupWorkflowRestoreAndroid::RecoveredReview*
BackupWorkflowRestoreAndroid::FindRecoveredReview(
    ProfileBackupWorkflow::WindowToken window,
    uint64_t token) {
  auto found = recovered_reviews_.find(token);
  return found != recovered_reviews_.end() && found->second->owner == window
             ? found->second.get()
             : nullptr;
}

bool BackupWorkflowRestoreAndroid::DiscoverInterruptedRestore(
    const jni_zero::JavaRef<jobject>& caller,
    ProfileBackupWorkflow::WindowToken window,
    uint64_t request_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const auto key = std::pair(window, request_token);
  if (!caller || request_token == 0u || !source_profile_ || !profile_manager_ ||
      !local_state_ || !workflow_ || !workflow_->IsActiveWindow(window) ||
      operations_.size() + pending_recovery_discoveries_.size() +
              recovered_reviews_.size() >=
          kMaximumRestoreOperations ||
      pending_recovery_discoveries_.contains(key)) {
    return false;
  }
  auto pending = std::make_unique<PendingRecoveryDiscovery>(
      window, request_token, profile_manager_, local_state_);
  pending->caller.Reset(caller);
  PendingRecoveryDiscovery* const owned = pending.get();
  pending_recovery_discoveries_.emplace(key, std::move(pending));
  owned->lifecycle->DiscoverInterruptedBackupRestore(
      source_profile_, BackupRestoreRestartDiscoveryMode::kReconcileCommit,
      base::BindOnce(
          &BackupWorkflowRestoreAndroid::OnInterruptedRestoreDiscovered,
          weak_factory_.GetWeakPtr(), window, request_token));
  return true;
}

void BackupWorkflowRestoreAndroid::AbandonInterruptedRestoreDiscovery(
    ProfileBackupWorkflow::WindowToken window,
    uint64_t request_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  pending_recovery_discoveries_.erase(std::pair(window, request_token));
}

void BackupWorkflowRestoreAndroid::OnInterruptedRestoreDiscovered(
    ProfileBackupWorkflow::WindowToken window,
    uint64_t request_token,
    BackupRestoreRestartDiscoveryResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const auto key = std::pair(window, request_token);
  auto found = pending_recovery_discoveries_.find(key);
  if (found == pending_recovery_discoveries_.end()) {
    return;
  }
  auto caller = std::move(found->second->caller);
  auto lifecycle = std::move(found->second->lifecycle);
  pending_recovery_discoveries_.erase(found);
  RetireLifecycle(std::move(lifecycle));
  if (!workflow_->IsActiveWindow(window)) {
    DeliverDiscovery(std::move(caller), request_token, 0u, nullptr, false,
                     kDiscoveryUnavailable);
    return;
  }
  if (!result) {
    DeliverDiscovery(std::move(caller), request_token, 0u, nullptr, false,
                     kDiscoveryUnavailable);
    return;
  }

  const bool rollback = result->status == DiscoveryStatus::kRollbackAvailable;
  const bool cleanup = result->status == DiscoveryStatus::kCleanupRequired;
  if (!rollback && !cleanup) {
    const int32_t status =
        result->candidate
            ? static_cast<int32_t>(DiscoveryStatus::kCustodyAmbiguous)
            : static_cast<int32_t>(result->status);
    DeliverDiscovery(std::move(caller), request_token, 0u, nullptr, false,
                     status);
    return;
  }
  if (!backup_workflow_restore_internal::IsValidRecoveredCandidate(*result)) {
    DeliverDiscovery(std::move(caller), request_token, 0u, nullptr, false,
                     static_cast<int32_t>(DiscoveryStatus::kCustodyAmbiguous));
    return;
  }
  const uint64_t token = MintRecoveredReviewToken();
  if (token == 0u) {
    DeliverDiscovery(std::move(caller), request_token, 0u, nullptr, false,
                     kDiscoveryUnavailable);
    return;
  }
  auto review = std::make_unique<RecoveredReview>(
      window, token, result->candidate->reservation_id, cleanup);
  recovered_reviews_.emplace(token, std::move(review));
  DeliverDiscovery(std::move(caller), request_token, token,
                   &result->candidate->presentation, cleanup,
                   static_cast<int32_t>(result->status));
}

bool BackupWorkflowRestoreAndroid::ResolveRecoveredRestore(
    const jni_zero::JavaRef<jobject>& caller,
    ProfileBackupWorkflow::WindowToken window,
    uint64_t recovered_review_token,
    mojom::BackupRestoreResolutionChoice choice) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  RecoveredReview* review = FindRecoveredReview(window, recovered_review_token);
  if (!caller || !review || review->phase != RecoveredReview::Phase::kReview ||
      review->resolution_caller || !workflow_->IsActiveWindow(window) ||
      !backup_workflow_restore_internal::RecoveryResolutionChoiceAllowed(
          review->discard_only, choice)) {
    return false;
  }
  review->resolution_caller.Reset(caller);
  review->phase = RecoveredReview::Phase::kResolving;
  review->resolution_lifecycle =
      std::make_unique<BrowserProfilesRestoreLifecycle>(profile_manager_,
                                                        local_state_);
  review->resolution_lifecycle->ResolveBackupRestoreCandidate(
      review->reservation_id, choice,
      base::BindOnce(
          &BackupWorkflowRestoreAndroid::OnRecoveredCandidateResolved,
          weak_factory_.GetWeakPtr(), recovered_review_token));
  return true;
}

void BackupWorkflowRestoreAndroid::OnRecoveredCandidateResolved(
    uint64_t recovered_review_token,
    BrowserProfilesRestoreLifecycle::CandidateResolutionResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = recovered_reviews_.find(recovered_review_token);
  if (found == recovered_reviews_.end() ||
      found->second->phase != RecoveredReview::Phase::kResolving) {
    return;
  }
  auto lifecycle = std::move(found->second->resolution_lifecycle);
  auto caller = std::move(found->second->resolution_caller);
  const bool detached = found->second->detached;
  const ResolutionStatus status =
      backup_workflow_restore_internal::ProjectCandidateResolution(result);
  RetireLifecycle(std::move(lifecycle));
  if (detached ||
      !backup_workflow_restore_internal::ResolutionRetainsHiddenCandidate(
          status)) {
    recovered_reviews_.erase(found);
  } else {
    found->second->phase = RecoveredReview::Phase::kReview;
  }
  if (!detached) {
    DeliverRecoveredResolution(std::move(caller), recovered_review_token,
                               status);
  }
}

void BackupWorkflowRestoreAndroid::AbandonRecoveredRestoreReview(
    ProfileBackupWorkflow::WindowToken window,
    uint64_t recovered_review_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = recovered_reviews_.find(recovered_review_token);
  if (found == recovered_reviews_.end() || found->second->owner != window) {
    return;
  }
  found->second->resolution_caller.Reset();
  if (found->second->phase == RecoveredReview::Phase::kResolving) {
    found->second->detached = true;
    return;
  }
  recovered_reviews_.erase(found);
}

void BackupWorkflowRestoreAndroid::WithdrawRecoveredWindow(
    ProfileBackupWorkflow::WindowToken window) {
  for (auto iterator = pending_recovery_discoveries_.begin();
       iterator != pending_recovery_discoveries_.end();) {
    if (iterator->second->owner == window) {
      iterator = pending_recovery_discoveries_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  for (auto iterator = recovered_reviews_.begin();
       iterator != recovered_reviews_.end();) {
    if (iterator->second->owner != window) {
      ++iterator;
    } else if (iterator->second->phase == RecoveredReview::Phase::kResolving) {
      iterator->second->detached = true;
      iterator->second->resolution_caller.Reset();
      ++iterator;
    } else {
      iterator = recovered_reviews_.erase(iterator);
    }
  }
}

}  // namespace taffy
