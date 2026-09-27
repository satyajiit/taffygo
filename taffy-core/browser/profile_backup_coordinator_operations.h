// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_OPERATIONS_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_OPERATIONS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "crypto/secure_util.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/profile_backup_coordinator_internal.h"

namespace taffy {

struct ProfileBackupCoordinator::ExportOperation {
  enum class Phase {
    kStartingCore,
    kReadingSnapshot,
    kValidatingSnapshot,
    kPreparingManifest,
    kSealingArchive,
  };

  ~ExportOperation() { crypto::SecureZeroBuffer(recovery_key); }

  Phase phase = Phase::kStartingCore;
  storage::backup::Secret recovery_key{};
  std::vector<core_service::mojom::BackupRecordKind> selection;
  std::unique_ptr<ValidatedBackupSnapshot> snapshot;
  ExportCallback callback;
};

struct ProfileBackupCoordinator::ImportOperation {
  enum class Phase {
    kAwaitingCopy,
    kStartingCore,
    kReadingVerifiedStage,
    kInspectingManifest,
    kHashingPayload,
    kPlanningRestore,
    kReady,
    kConfirmingRestore,
    kStagingRestore,
    kStaged,
    kAuthorizingCommit,
    kCommittingRestore,
    kReportingCommit,
    kHiddenReview,
    kCommitRecoveryRequired,
    kCancellingRestore,
  };

  Phase phase = Phase::kAwaitingCopy;
  std::string source_profile_id;
  std::string target_profile_id;
  std::unique_ptr<ProfileBackupRestoreTarget> target;
  core_service::mojom::BackupManifestInspectResultPtr inspection;
  std::unique_ptr<HashedBackupImport> imported;
  core_service::mojom::BackupRestorePlanResultPtr plan;
  core_service::mojom::OperationEnvelopePtr confirmation_operation;
  core_service::mojom::BackupRestoreStageAuthorizationPtr stage_authorization;
  std::vector<core_service::mojom::SkillRecordPtr> staged_procedures;
  core_service::mojom::OperationEnvelopePtr commit_operation;
  core_service::mojom::BackupRestoreCommitAuthorizationPtr commit_authorization;
  std::optional<ProfileBackupRestoreTarget::CommitResult> commit_result;
  bool commit_authority_requested = false;
  bool commit_dispatched = false;
  bool target_close_requested = false;
  std::optional<CoreBackupProtocol::WorkflowInterest> workflow_interest;
  bool cancellation_requested = false;
  bool cancellation_rpc_in_flight = false;
  bool portable_cancellation_complete = false;
  bool target_cleanup_in_flight = false;
  bool target_cleanup_retry_requested = false;
  bool target_cleanup_complete = false;
  ProfileBackupError cancellation_error = ProfileBackupError::kCancelled;
  RestorePreviewCallback callback;
  RestoreStageCallback stage_callback;
  RestoreCommitCallback commit_callback;
  RestoreStageCallback close_callback;
  PrecommitCancellationCallback precommit_cancellation_callback;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_OPERATIONS_H_
