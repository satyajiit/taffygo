// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Presentation only: no keys, archive contents, document URIs or native operation identities. */
data class BackupUiState(
    val selection: Set<BackupWindowHost.ContentClass> = emptySet(),
    val step: Step = Step.IDLE,
    val notice: Notice = Notice.NONE,
    val resumed: Boolean = false,
    val restore: BackupRestoreUiState? = null,
    val restoreName: String? = null,
    val documentCleanup: BackupDocumentCleanupStep? = null,
    val cleanupSource: CleanupSource? = null,
    val documentName: String? = null,
    /**
     * Whether the screen offers Restore and the review of an interrupted
     * restore. Nothing sets it in 1.0: a restore creates the profile it
     * restores into, and Chromium on Android builds only its first profile, so
     * the second one aborts the browser (decision 0255, OD-132). Creating a
     * backup and checking one are unaffected, and the restore machinery stays
     * compiled and tested behind this one switch.
     */
    val restoreOffered: Boolean = false,
) {
    enum class Step {
        IDLE, RECOVERY_KEY, PREPARING_EXPORT, PICK_EXPORT, WAITING_EXPORT,
        WRITING_EXPORT, PICK_IMPORT, WAITING_IMPORT, CHECKING_IMPORT, RESTORE_IMPORTED, RESTORING,
        PICK_DELETE, WAITING_DELETE, PREPARING_DELETE, RECOVERING,
    }

    enum class CleanupSource { EXPORT, SELECTED }

    enum class Notice {
        NONE, SAVED, INCOMPLETE, CHECKED_ONLY, EXPORT_REFUSED, IMPORT_REFUSED, UNAVAILABLE, CANCELLED,
        RESTORE_REFUSED, RESTORE_NEEDS_REVIEW, RESTORE_KEPT,
        RESTORE_DISCARDED, RESTORE_STOPPED,
        COPY_DELETED, COPY_STILL_PRESENT, COPY_DELETE_UNVERIFIED,
        DELETE_SELECTION_UNAVAILABLE, DELETE_SELECTION_CANCELLED,
        RECOVERY_NONE, RECOVERY_SOURCE_UNAVAILABLE, RECOVERY_UNAVAILABLE,
        RECOVERY_ALREADY_KEPT, RECOVERY_ALREADY_DISCARDED, RECOVERY_CLOSED,
    }

    val busy: Boolean get() = step != Step.IDLE || documentCleanup in setOf(
        BackupDocumentCleanupStep.CONFIRMING, BackupDocumentCleanupStep.DELETING,
    )
    val canCreate: Boolean get() = resumed && !busy && selection.isNotEmpty()
    val canCheck: Boolean get() = resumed && !busy
    val canCheckRecovery: Boolean get() = resumed && (!busy || restore?.step == BackupRestoreUiState.Step.NEEDS_RECOVERY)
    val canCancel: Boolean get() = step != Step.IDLE && (restore?.mayCancelPreparation != false)

    companion object {
        const val MAXIMUM_RESTORE_NAME = 40
    }
}
