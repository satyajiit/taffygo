// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.net.Uri

/** One regular, resumed browser window's trusted backup entry point. */
interface BackupWindowHost {
    /** The six record families whose typed snapshot and restore codecs exist. */
    enum class ContentClass {
        ASSISTANT_CONFIGURATION,
        SAVED_WORKSPACES,
        LIBRARY,
        MEMORY,
        USER_AUTHORED_SKILLS,
        LEARNED_PROCEDURES,
    }

    sealed interface ExportPreparation {
        data class Ready(val archiveBytes: Long) : ExportPreparation
        data object Refused : ExportPreparation
        data object Unavailable : ExportPreparation
    }

    /** Returns null unless this exact regular-profile window is resumed. */
    fun openRecoveryKeySession(mode: BackupRecoveryKeySession.Mode): BackupRecoveryKeySession?

    /** Fresh file selection only; returns null unless this exact regular window is resumed. */
    fun openBackupDeletionRequest(): BackupDeletionRequest?

    /**
     * Consumes this window's exact request and reads display-name metadata only. Never opens archive
     * contents or deletes anything. Null means no usable selection; a returned copy needs a separate
     * resumed confirmation. A stale receiver must close it. The picker may still own the foreground.
     */
    suspend fun completeBackupDeletionSelection(request: BackupDeletionRequest, source: Uri): BackupDocumentCopy?

    /** Consumes a confirmed create key and prepares one immutable selected snapshot. */
    fun prepareExport(
        session: BackupRecoveryKeySession,
        selection: Set<ContentClass>,
        onResult: (ExportPreparation) -> Unit,
    )

    /**
     * Copies one prepared encrypted archive and authenticates readback. After the transfer drains,
     * the result may carry the exact destination for a separate explicit cleanup choice, including
     * an incomplete write. A stale receiver must close that handle without deleting anything.
     */
    suspend fun writePreparedExport(
        session: BackupRecoveryKeySession,
        destination: Uri,
    ): BackupExportResult

    /** Copies one selected encrypted archive into native custody and authenticates it. */
    suspend fun copyAndInspectImport(
        session: BackupRecoveryKeySession,
        source: Uri,
    ): BackupDocumentImport.ImportStatus

    /**
     * Reserves a hidden new profile and asks the source Core to plan the authenticated import.
     * The localized label is presentation metadata; native custody chooses every identity/path.
     */
    fun prepareImportedRestore(
        session: BackupRecoveryKeySession,
        targetProfileLabel: String,
        onResult: (BackupRestorePreparation) -> Unit,
    )

    /** Confirms the exact native-held plan and builds its reversible isolated SQL stage. */
    fun confirmAndStageImportedRestore(
        review: BackupRestoreReview,
        onResult: (BackupRestoreStageResult) -> Unit,
    )

    /**
     * Commits the staged SQL candidate and closes its writer before returning review state. A
     * definite noncommit retains the exact review only for an explicit discard cleanup; it never
     * permits Accept or another commit.
     */
    fun commitStagedImportedRestore(
        review: BackupRestoreReview,
        onResult: (BackupRestoreCommitResult) -> Unit,
    )

    /**
     * Requests a fresh source-Core decision for one still-hidden candidate. A definitely
     * unfinished, refused, or unavailable result retains that exact candidate but grants no retry;
     * only another explicit choice may request fresh authority. Post-dispatch uncertainty is
     * [BackupRestoreResolutionResult.RECOVERY_REQUIRED].
     */
    fun resolveImportedRestore(
        review: BackupRestoreReview,
        choice: BackupRestoreResolutionChoice,
        onResult: (BackupRestoreResolutionResult) -> Unit,
    )

    /**
     * Observes this source profile's interrupted restore without a key or replayed operation.
     * The callback runs on the UI thread. Null means no admission; an admitted request may answer
     * inline. Closing the request withdraws undelivered presentation, not an already delivered review.
     */
    fun discoverInterruptedRestore(
        onResult: (BackupRestoreDiscoveryResult) -> Unit,
    ): BackupRestoreDiscoveryRequest?

    /** Nonthrowing withdrawal of this window's exact review, never a discard or undo request. */
    fun abandonImportedRestoreReview(review: BackupRestoreReview)

    /** Idempotently withdraws this host's exact native operation. */
    suspend fun abandon(session: BackupRecoveryKeySession)
}
