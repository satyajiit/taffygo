// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.annotation.StringRes

@StringRes
internal fun backupContentLabel(content: BackupWindowHost.ContentClass): Int = when (content) {
    BackupWindowHost.ContentClass.ASSISTANT_CONFIGURATION -> R.string.taffy_backup_content_taffy
    BackupWindowHost.ContentClass.SAVED_WORKSPACES -> R.string.taffy_backup_content_workspaces
    BackupWindowHost.ContentClass.LIBRARY -> R.string.taffy_backup_content_library
    BackupWindowHost.ContentClass.MEMORY -> R.string.taffy_backup_content_memory
    BackupWindowHost.ContentClass.USER_AUTHORED_SKILLS -> R.string.taffy_backup_content_skills
    BackupWindowHost.ContentClass.LEARNED_PROCEDURES -> R.string.taffy_backup_content_learned
}

@StringRes
internal fun backupStepLabel(step: BackupUiState.Step): Int = when (step) {
    BackupUiState.Step.RECOVERY_KEY -> R.string.taffy_backup_waiting_key
    BackupUiState.Step.PREPARING_EXPORT -> R.string.taffy_backup_preparing
    BackupUiState.Step.PICK_EXPORT, BackupUiState.Step.WAITING_EXPORT,
    BackupUiState.Step.PICK_IMPORT, BackupUiState.Step.WAITING_IMPORT,
    BackupUiState.Step.PICK_DELETE, BackupUiState.Step.WAITING_DELETE,
    -> R.string.taffy_backup_waiting_document
    BackupUiState.Step.WRITING_EXPORT -> R.string.taffy_backup_writing
    BackupUiState.Step.CHECKING_IMPORT -> R.string.taffy_backup_checking
    BackupUiState.Step.PREPARING_DELETE -> R.string.taffy_backup_delete_reading_name
    BackupUiState.Step.RESTORE_IMPORTED -> R.string.taffy_backup_restore_file_checked
    BackupUiState.Step.RESTORING -> R.string.taffy_backup_restore_planning
    BackupUiState.Step.RECOVERING -> R.string.taffy_backup_recovery_checking
    BackupUiState.Step.IDLE -> R.string.taffy_backup_title
}

@StringRes
internal fun backupNoticeLabel(notice: BackupUiState.Notice): Int = when (notice) {
    BackupUiState.Notice.SAVED -> R.string.taffy_backup_saved
    BackupUiState.Notice.INCOMPLETE -> R.string.taffy_backup_incomplete
    BackupUiState.Notice.CHECKED_ONLY -> R.string.taffy_backup_checked_only
    BackupUiState.Notice.EXPORT_REFUSED -> R.string.taffy_backup_export_refused
    BackupUiState.Notice.IMPORT_REFUSED -> R.string.taffy_backup_import_refused
    BackupUiState.Notice.UNAVAILABLE -> R.string.taffy_backup_unavailable
    BackupUiState.Notice.CANCELLED -> R.string.taffy_backup_cancelled
    BackupUiState.Notice.RESTORE_REFUSED -> R.string.taffy_backup_restore_refused
    BackupUiState.Notice.RESTORE_NEEDS_REVIEW -> R.string.taffy_backup_restore_needs_review
    BackupUiState.Notice.RESTORE_KEPT -> R.string.taffy_backup_restore_kept
    BackupUiState.Notice.RESTORE_DISCARDED -> R.string.taffy_backup_restore_discarded
    BackupUiState.Notice.RESTORE_STOPPED -> R.string.taffy_backup_restore_stopped
    BackupUiState.Notice.COPY_DELETED -> R.string.taffy_backup_copy_deleted
    BackupUiState.Notice.COPY_STILL_PRESENT -> R.string.taffy_backup_copy_still_present
    BackupUiState.Notice.COPY_DELETE_UNVERIFIED -> R.string.taffy_backup_copy_delete_unverified
    BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE -> R.string.taffy_backup_delete_selection_unavailable
    BackupUiState.Notice.DELETE_SELECTION_CANCELLED -> R.string.taffy_backup_delete_selection_cancelled
    BackupUiState.Notice.RECOVERY_NONE -> R.string.taffy_backup_recovery_none
    BackupUiState.Notice.RECOVERY_SOURCE_UNAVAILABLE -> R.string.taffy_backup_recovery_source_unavailable
    BackupUiState.Notice.RECOVERY_UNAVAILABLE -> R.string.taffy_backup_recovery_unavailable
    BackupUiState.Notice.RECOVERY_ALREADY_KEPT -> R.string.taffy_backup_recovery_already_kept
    BackupUiState.Notice.RECOVERY_ALREADY_DISCARDED -> R.string.taffy_backup_recovery_already_discarded
    BackupUiState.Notice.RECOVERY_CLOSED -> R.string.taffy_backup_recovery_closed
    BackupUiState.Notice.NONE -> R.string.taffy_backup_title
}

@StringRes
internal fun backupRestoreStepLabel(step: BackupRestoreUiState.Step): Int = when (step) {
    BackupRestoreUiState.Step.DISCOVERING -> R.string.taffy_backup_recovery_checking
    BackupRestoreUiState.Step.PLANNING -> R.string.taffy_backup_restore_planning
    BackupRestoreUiState.Step.REVIEW -> R.string.taffy_backup_restore_review_title
    BackupRestoreUiState.Step.STAGING -> R.string.taffy_backup_restore_staging
    BackupRestoreUiState.Step.STAGED -> R.string.taffy_backup_restore_staged
    BackupRestoreUiState.Step.COMMITTING -> R.string.taffy_backup_restore_committing
    BackupRestoreUiState.Step.CANDIDATE -> R.string.taffy_backup_restore_candidate
    BackupRestoreUiState.Step.CLEANUP_REQUIRED -> R.string.taffy_backup_restore_not_committed
    BackupRestoreUiState.Step.CONFIRM_DISCARD -> R.string.taffy_backup_restore_discard_title
    BackupRestoreUiState.Step.RESOLVING -> R.string.taffy_backup_restore_resolving
    BackupRestoreUiState.Step.NEEDS_RECOVERY -> R.string.taffy_backup_restore_needs_review
}

@StringRes
internal fun backupRecoveryReasonLabel(reason: BackupRestoreDiscoveryResult.Reason): Int = when (reason) {
    BackupRestoreDiscoveryResult.Reason.PRECOMMIT -> R.string.taffy_backup_recovery_precommit
    BackupRestoreDiscoveryResult.Reason.PRESENTATION_UNAVAILABLE -> R.string.taffy_backup_recovery_details_missing
    BackupRestoreDiscoveryResult.Reason.SCHEMA_MISMATCH -> R.string.taffy_backup_recovery_format_mismatch
    BackupRestoreDiscoveryResult.Reason.OUTCOME_UNKNOWN -> R.string.taffy_backup_recovery_unknown
    BackupRestoreDiscoveryResult.Reason.CUSTODY_AMBIGUOUS -> R.string.taffy_backup_recovery_busy_or_changed
}
