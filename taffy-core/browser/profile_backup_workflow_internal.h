// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_INTERNAL_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_INTERNAL_H_

#include <cstdint>

#include "base/memory/ref_counted.h"
#include "taffy/browser/profile_backup_workflow.h"
#include "taffy/browser/profile_backup_workflow_io.h"

namespace taffy {

struct ProfileBackupWorkflow::Operation {
  enum class Phase : uint8_t {
    kCreateKey,
    kCreateConfirmed,
    kExportPreparing,
    kExportReady,
    kRestoreKey,
    kImportStarting,
    kImportReady,
    kImportVerified,
    kRestoreTargetPreparing,
    kRestorePlanning,
    kRestorePresentationPersisting,
    kRestorePreview,
    kRestoreStaging,
    kRestoreStaged,
    kRestoreCommitting,
    kRestoreClosing,
    kHiddenReview,
    kRestoreDefinitelyNotCommitted,
    kRestoreRecoveryRequired,
    kRestoreResolving,
    kPrecommitCleanupRequired,
  };

  // Defined in profile_backup_workflow.cc. This struct is complex enough that
  // the chromium-style plugin refuses an inlined constructor or destructor,
  // and that file is the one translation unit that creates and retires it.
  Operation(WindowToken owner, Phase initial_phase);
  ~Operation();

  const WindowToken owner;
  Phase phase;
  storage::backup::Secret key{};
  bool display_taken = false;
  uint64_t archive_bytes = 0u;
  uint64_t maximum_import_bytes = 0u;
  RestoreReviewToken restore_review_token = 0u;
  std::u16string target_profile_label;
  std::string reservation_id;
  std::vector<uint8_t> confirmation_digest;
  bool restore_can_stage = false;
  // A known definitely-not-committed target may be resolved only by an
  // explicit Discard decision. Preserve that restriction across refused or
  // definitely-not-completed resolution attempts.
  bool restore_discard_only_resolution = false;
  // Window withdrawal may revoke presentation while a consumed commit still
  // has to drain through writer close. No callback may reach that window, but
  // the operation remains until the physical close barrier settles.
  bool restore_window_detached = false;
  RestorePresentationWriter restore_presentation_writer;
  std::optional<BackupRestoreRecoveryPresentation>
      expected_restore_presentation;
  RestorePreparationCallback restore_preparation_callback;
  RestoreStageCallback restore_stage_callback;
  RestoreCommitCallback restore_commit_callback;
  RestoreResolutionCallback restore_resolution_callback;
  scoped_refptr<ProfileBackupWorkflowIoState> io_state;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_INTERNAL_H_
