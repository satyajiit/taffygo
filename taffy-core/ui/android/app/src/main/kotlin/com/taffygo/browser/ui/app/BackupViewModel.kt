// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.net.Uri
import androidx.annotation.MainThread
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Window-bound UI ceremony. Never restores a key, picker grant or native operation from saved state. */
@MainThread
class BackupViewModel(
    private val host: BackupWindowHost,
    private val analytics: AnalyticsClient,
) : ViewModel() {
    private val local = MutableStateFlow(BackupUiState())
    val state: StateFlow<BackupUiState> = local.asStateFlow()
    private var session: BackupRecoveryKeySession? = null
    private var transfer: Job? = null
    private var restoring = false
    private var restoreCeremony: BackupRestoreCeremony? = null
    private var documentCleanup: BackupDocumentCleanup? = null
    private var deletionSelection: BackupDeletionSelection? = null
    private var restartRecovery: BackupRestartRecovery? = null

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.Backup.screenId))
    }

    fun onIntent(intent: BackupIntent) {
        when (intent) {
            BackupIntent.Create -> if (state.value.canCreate) begin(BackupRecoveryKeySession.Mode.CREATE)
            BackupIntent.Check -> if (state.value.canCheck) begin(BackupRecoveryKeySession.Mode.RESTORE)
            BackupIntent.Restore -> if (state.value.canCheck) begin(BackupRecoveryKeySession.Mode.RESTORE, true)
            BackupIntent.ChooseBackupForDeletion -> if (state.value.canCheck) beginDeletionSelection()
            BackupIntent.ReviewInterruptedRestore -> if (state.value.canCheckRecovery) beginRestartRecovery()
            is BackupIntent.ReviewRestore -> reviewImportedRestore(intent.targetProfileLabel)
            BackupIntent.StageRestore, BackupIntent.CommitRestore, BackupIntent.AcceptRestore,
            BackupIntent.RequestDiscardRestore, BackupIntent.DiscardRestore, BackupIntent.BackToRestoreReview,
            -> if (restartRecovery != null) restartRecovery?.onIntent(intent, state.value.resumed)
                else restoreCeremony?.onIntent(intent, state.value.resumed)
            BackupIntent.RequestDeleteCopy, BackupIntent.KeepDocumentCopy, BackupIntent.DeleteDocumentCopy,
            -> documentCleanup?.onIntent(intent, state.value.resumed)
            BackupIntent.Cancel -> if (state.value.canCancel) cancel()
            else -> local.value = reduceBackup(state.value, intent)
        }
    }

    /** Only the trusted key dialog receives this opaque port, not the immutable screen state. */
    fun keySession(): BackupRecoveryKeySession? =
        session.takeIf { state.value.step == BackupUiState.Step.RECOVERY_KEY }

    fun onKeyOutcome(owned: BackupRecoveryKeySession, outcome: BackupRecoveryKeySession.Outcome) {
        if (session !== owned || state.value.step != BackupUiState.Step.RECOVERY_KEY) return
        when (outcome) {
            BackupRecoveryKeySession.Outcome.CANCELLED -> cancel()
            BackupRecoveryKeySession.Outcome.UNAVAILABLE -> finish(owned, BackupUiState.Notice.UNAVAILABLE)
            BackupRecoveryKeySession.Outcome.CONFIRMED -> {
                if (owned.mode == BackupRecoveryKeySession.Mode.CREATE) prepare(owned)
                else local.value = state.value.copy(step = BackupUiState.Step.PICK_IMPORT)
            }
        }
    }

    /** Claim before launching Android so recomposition cannot launch the picker twice. */
    fun claimDocumentRequest(): BackupRecoveryKeySession? {
        val owned = session ?: return null
        val next = when (state.value.step) {
            BackupUiState.Step.PICK_EXPORT -> BackupUiState.Step.WAITING_EXPORT
            BackupUiState.Step.PICK_IMPORT -> BackupUiState.Step.WAITING_IMPORT
            else -> return null
        }
        local.value = state.value.copy(step = next)
        return owned
    }

    /** A stale Android result has no authority over a newer operation or a recreated window. */
    fun onDocumentResult(owned: BackupRecoveryKeySession, document: Uri?) {
        if (session !== owned) return
        val exporting = when (state.value.step) {
            BackupUiState.Step.WAITING_EXPORT -> true
            BackupUiState.Step.WAITING_IMPORT -> false
            else -> return
        }
        if (document == null) {
            cancel()
            return
        }
        local.value = state.value.copy(step = if (exporting) {
            BackupUiState.Step.WRITING_EXPORT
        } else {
            BackupUiState.Step.CHECKING_IMPORT
        })
        val wantsRestore = restoring
        transfer = viewModelScope.launch(start = CoroutineStart.LAZY) {
            var exportedCopy: BackupDocumentCopy? = null
            try {
                var retainedForRestore = false
                val notice = try {
                    if (exporting) {
                        val result = host.writePreparedExport(owned, document)
                        exportedCopy = result.copy
                        if (result.status == BackupDocumentTransfer.WriteResult.UNAVAILABLE) {
                            exportedCopy?.close()
                            exportedCopy = null
                        }
                        currentCoroutineContext().ensureActive()
                        backupWriteNotice(result.status)
                    } else {
                        val result = host.copyAndInspectImport(owned, document)
                        currentCoroutineContext().ensureActive()
                        if (wantsRestore && result == BackupDocumentImport.ImportStatus.VERIFIED && session === owned) {
                            retainedForRestore = true
                            local.value = state.value.copy(step = BackupUiState.Step.RESTORE_IMPORTED)
                            BackupUiState.Notice.NONE
                        } else backupImportNotice(result)
                    }
                } catch (cancelled: CancellationException) {
                    currentCoroutineContext().ensureActive()
                    if (exporting) BackupUiState.Notice.INCOMPLETE else BackupUiState.Notice.UNAVAILABLE
                } catch (_: RuntimeException) {
                    if (exporting) BackupUiState.Notice.INCOMPLETE else BackupUiState.Notice.UNAVAILABLE
                } finally {
                    // Only the explicit Restore flow may retain a verified stage.
                    // A check, failure or withdrawn session always relinquishes it.
                    if (!retainedForRestore) withContext(NonCancellable) {
                        try {
                            host.abandon(owned)
                        } catch (_: RuntimeException) {
                            owned.cancel()
                        }
                    }
                }
                if (retainedForRestore) {
                    if (session === owned) transfer = null
                } else {
                    finish(owned, notice, exportedCopy)
                    exportedCopy = null
                }
            } finally {
                exportedCopy?.close()
            }
        }
        transfer?.start()
    }

    fun onPickerUnavailable(owned: BackupRecoveryKeySession) {
        if (session === owned) finish(owned, BackupUiState.Notice.UNAVAILABLE)
    }

    fun claimDeletionDocumentRequest(): BackupDeletionRequest? = deletionSelection?.claimRequest()

    fun onDeletionDocumentResult(owned: BackupDeletionRequest, document: Uri?) {
        deletionSelection?.onResult(owned, document)
    }

    fun onDeletionPickerUnavailable(owned: BackupDeletionRequest) {
        deletionSelection?.onPickerUnavailable(owned)
    }

    fun onResumed(resumed: Boolean) {
        local.value = state.value.copy(resumed = resumed)
        if (!resumed && state.value.step in setOf(
                BackupUiState.Step.RECOVERY_KEY, BackupUiState.Step.PREPARING_EXPORT,
                BackupUiState.Step.PICK_EXPORT, BackupUiState.Step.PICK_IMPORT,
                BackupUiState.Step.PICK_DELETE,
            )
        ) cancel()
        // A confirmed, already-launched Android document handoff may finish
        // while its picker owns the foreground. Disposal still withdraws it.
    }

    fun onHidden() {
        forgetDocumentCopy()
        cancel()
    }

    override fun onCleared() {
        onHidden()
    }

    private fun begin(mode: BackupRecoveryKeySession.Mode, restore: Boolean = false) {
        forgetDocumentCopy()
        val owned = try {
            host.openRecoveryKeySession(mode)
        } catch (_: RuntimeException) {
            null
        }
        if (owned == null || owned.mode != mode) {
            owned?.cancel()
            local.value = state.value.copy(notice = BackupUiState.Notice.UNAVAILABLE)
            return
        }
        session = owned
        restoring = restore
        local.value = state.value.copy(
            step = BackupUiState.Step.RECOVERY_KEY, notice = BackupUiState.Notice.NONE, restoreName = null,
        )
    }

    private fun beginDeletionSelection() {
        forgetDocumentCopy()
        val selection = BackupDeletionSelection(
            host, viewModelScope,
            publish = { next -> local.value = state.value.copy(step = next, notice = BackupUiState.Notice.NONE) },
            finish = { copy, notice ->
                deletionSelection = null
                local.value = state.value.copy(step = BackupUiState.Step.IDLE, notice = notice)
                if (copy != null) offerDocumentCopy(copy, BackupUiState.CleanupSource.SELECTED)
            },
        )
        deletionSelection = selection
        selection.start()
    }

    private fun reviewImportedRestore(targetProfileLabel: String) {
        val owned = session ?: return
        if (!restoring || !state.value.resumed || state.value.step != BackupUiState.Step.RESTORE_IMPORTED ||
            !isBackupRestoreNameReady(targetProfileLabel)
        ) return
        local.value = state.value.copy(step = BackupUiState.Step.RESTORING)
        val ceremony = BackupRestoreCeremony(
            host, viewModelScope,
            publish = { restore ->
                if (session === owned) local.value = state.value.copy(restore = restore)
            },
            finish = { notice -> finish(owned, notice) },
        )
        restoreCeremony = ceremony
        ceremony.start(owned, targetProfileLabel)
    }

    private fun beginRestartRecovery() {
        forgetDocumentCopy()
        cancel()
        local.value = state.value.copy(step = BackupUiState.Step.RECOVERING, notice = BackupUiState.Notice.NONE)
        val recovery = BackupRestartRecovery(
            host, viewModelScope,
            publish = { next -> local.value = state.value.copy(restore = next) },
            finish = { notice ->
                restartRecovery = null
                local.value = state.value.copy(step = BackupUiState.Step.IDLE, restore = null, notice = notice)
            },
        )
        restartRecovery = recovery
        recovery.start()
    }

    private fun prepare(owned: BackupRecoveryKeySession) {
        local.value = state.value.copy(step = BackupUiState.Step.PREPARING_EXPORT)
        try {
            host.prepareExport(owned, state.value.selection.toSet()) { result ->
                viewModelScope.launch {
                    if (session !== owned || state.value.step != BackupUiState.Step.PREPARING_EXPORT) return@launch
                    when (result) {
                        is BackupWindowHost.ExportPreparation.Ready -> {
                            if (result.archiveBytes <= 0) finish(owned, BackupUiState.Notice.UNAVAILABLE)
                            else local.value = state.value.copy(step = BackupUiState.Step.PICK_EXPORT)
                        }
                        BackupWindowHost.ExportPreparation.Refused -> finish(owned, BackupUiState.Notice.EXPORT_REFUSED)
                        BackupWindowHost.ExportPreparation.Unavailable -> finish(owned, BackupUiState.Notice.UNAVAILABLE)
                    }
                }
            }
        } catch (_: RuntimeException) {
            finish(owned, BackupUiState.Notice.UNAVAILABLE)
        }
    }

    private fun finish(
        owned: BackupRecoveryKeySession,
        notice: BackupUiState.Notice,
        copy: BackupDocumentCopy? = null,
    ) {
        if (session !== owned) {
            copy?.close()
            return
        }
        session = null
        restoreCeremony?.close()
        restoreCeremony = null
        restoring = false
        owned.cancel()
        transfer = null
        local.value = state.value.copy(step = BackupUiState.Step.IDLE, notice = notice, restore = null, restoreName = null)
        if (copy != null) offerDocumentCopy(copy, BackupUiState.CleanupSource.EXPORT)
    }

    private fun offerDocumentCopy(copy: BackupDocumentCopy, source: BackupUiState.CleanupSource) {
        val name = try {
            if (source == BackupUiState.CleanupSource.EXPORT) null else
                copy.displayName?.takeIf(::isBackupDocumentNameSafe)
                    ?: throw IllegalStateException("Missing usable document name")
        } catch (_: RuntimeException) {
            copy.close()
            local.value = state.value.copy(notice = BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE)
            return
        }
        documentCleanup = BackupDocumentCleanup(
            copy, viewModelScope,
            publish = { next -> local.value = state.value.copy(
                documentCleanup = next,
                notice = if (next == BackupDocumentCleanupStep.AVAILABLE) state.value.notice
                else BackupUiState.Notice.NONE,
            ) },
            finish = { result ->
                documentCleanup = null
                local.value = state.value.copy(
                    documentCleanup = null, cleanupSource = null, documentName = null, notice = result,
                )
            },
        )
        local.value = state.value.copy(
            documentCleanup = BackupDocumentCleanupStep.AVAILABLE, cleanupSource = source, documentName = name,
        )
    }

    private fun forgetDocumentCopy() {
        val deleting = state.value.documentCleanup == BackupDocumentCleanupStep.DELETING
        documentCleanup?.close()
        documentCleanup = null
        local.value = state.value.copy(
            documentCleanup = null,
            cleanupSource = null,
            documentName = null,
            notice = if (deleting) BackupUiState.Notice.COPY_DELETE_UNVERIFIED else state.value.notice,
        )
    }

    private fun cancel() {
        restartRecovery?.let {
            restartRecovery = null
            it.close()
            local.value = state.value.copy(
                step = BackupUiState.Step.IDLE, restore = null, notice = BackupUiState.Notice.RECOVERY_CLOSED,
            )
        }
        deletionSelection?.let {
            deletionSelection = null
            it.close()
            local.value = state.value.copy(
                step = BackupUiState.Step.IDLE, notice = BackupUiState.Notice.DELETE_SELECTION_CANCELLED,
            )
        }
        val owned = session ?: return
        session = null
        val writing = state.value.step == BackupUiState.Step.WRITING_EXPORT
        val restoreNeedsReview = state.value.restore?.mayCancelPreparation == false
        val wasRestoring = restoring
        transfer?.cancel()
        transfer = null
        restoreCeremony?.close()
        restoreCeremony = null
        restoring = false
        owned.cancel()
        local.value = state.value.copy(
            step = BackupUiState.Step.IDLE,
            restore = null,
            restoreName = null,
            notice = when {
                restoreNeedsReview -> BackupUiState.Notice.RESTORE_NEEDS_REVIEW
                wasRestoring -> BackupUiState.Notice.RESTORE_STOPPED
                writing -> BackupUiState.Notice.INCOMPLETE
                else -> BackupUiState.Notice.CANCELLED
            },
        )
    }
}
