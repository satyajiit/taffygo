// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/backup_workflow_restore_projection.h"

#include <limits>

namespace taffy::backup_workflow_restore_internal {

namespace mojom = core_service::mojom;
using Error = BackupRestoreCandidateResolutionError;
using Status = ProfileBackupWorkflow::RestoreResolutionStatus;

ProfileBackupWorkflow::RestoreResolutionStatus ProjectCandidateResolution(
    const BrowserProfilesRestoreLifecycle::CandidateResolutionResult& result) {
  if (!result) {
    switch (result.error()) {
      case Error::kInvalidArgument:
        // Closed input validation fails before a resolution operation exists.
        return Status::kRefused;
      case Error::kBusy:
        // This caller did not consume authority, but a process-local target
        // lease may be busy because another owner already crossed its physical
        // linearization point. Never infer that the candidate is unchanged or
        // retain a retry capability from this observation.
        return Status::kRecoveryRequired;
      case Error::kProfileStateRefused:
      case Error::kCoreUnavailable:
      case Error::kHistoryRefused:
      case Error::kRegistryRefused:
      case Error::kPersistenceFailed:
      case Error::kStorageUnavailable:
        return Status::kRecoveryRequired;
    }
  }
  if (!result->classification) {
    return Status::kRecoveryRequired;
  }
  switch (result->classification->kind) {
    case mojom::BackupRestoreRecoveryClassificationKind::kPublished:
      return result->reservation_retired ? Status::kPublished
                                         : Status::kRecoveryRequired;
    case mojom::BackupRestoreRecoveryClassificationKind::kVerifiedDeleted:
      return result->reservation_retired ? Status::kVerifiedDeleted
                                         : Status::kRecoveryRequired;
    case mojom::BackupRestoreRecoveryClassificationKind::kRollbackAvailable:
    case mojom::BackupRestoreRecoveryClassificationKind::kCleanupRequired:
      // Portable history proves the attempt definitely did not complete.
      // The dispatch flag remains audit evidence, not a retry-authority gate.
      return Status::kDefinitelyNotCompleted;
    case mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired:
      return Status::kRecoveryRequired;
  }
  return Status::kRecoveryRequired;
}

bool ResolutionRetainsHiddenCandidate(Status status) {
  switch (status) {
    case Status::kDefinitelyNotCompleted:
    case Status::kRefused:
    case Status::kUnavailable:
      return true;
    case Status::kPublished:
    case Status::kVerifiedDeleted:
    case Status::kRecoveryRequired:
      return false;
  }
  return false;
}

bool CanReleasePrecommitOperation(bool cleanup_settled,
                                  bool commit_completion_pending) {
  return cleanup_settled && !commit_completion_pending;
}

std::optional<std::vector<int32_t>> FlattenRecoveryPresentation(
    const BackupRestoreRecoveryPresentation& presentation) {
  constexpr size_t kFieldsPerClass = 7u;
  static_assert(mojom::kMaxBackupRecords <=
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  if (presentation.target_profile_label.empty() ||
      presentation.selected_classes.empty() ||
      presentation.selected_classes.size() > 6u) {
    return std::nullopt;
  }
  std::vector<int32_t> flattened;
  flattened.reserve(presentation.selected_classes.size() * kFieldsPerClass);
  for (const auto& row : presentation.selected_classes) {
    const int32_t kind = static_cast<int32_t>(row.kind);
    if (kind < 0 || kind > 5) {
      return std::nullopt;
    }
    flattened.push_back(kind);
    for (uint32_t count : row.action_counts) {
      if (count > mojom::kMaxBackupRecords) {
        return std::nullopt;
      }
      flattened.push_back(static_cast<int32_t>(count));
    }
  }
  return flattened;
}

bool IsValidRecoveredCandidate(const BackupRestoreRestartDiscovery& discovery) {
  using Action = BackupRestoreRestartCandidateAction;
  using Discovery = BackupRestoreRestartDiscoveryStatus;
  const bool rollback = discovery.status == Discovery::kRollbackAvailable;
  const bool cleanup = discovery.status == Discovery::kCleanupRequired;
  return (rollback || cleanup) && discovery.candidate &&
         !discovery.candidate->reservation_id.empty() &&
         discovery.candidate->action ==
             (rollback ? Action::kReview : Action::kDiscardOnly) &&
         FlattenRecoveryPresentation(discovery.candidate->presentation)
             .has_value();
}

bool RecoveryResolutionChoiceAllowed(
    bool discard_only,
    mojom::BackupRestoreResolutionChoice choice) {
  switch (choice) {
    case mojom::BackupRestoreResolutionChoice::kAcceptCandidate:
      return !discard_only;
    case mojom::BackupRestoreResolutionChoice::kDiscardCandidate:
      return true;
  }
  return false;
}

}  // namespace taffy::backup_workflow_restore_internal
