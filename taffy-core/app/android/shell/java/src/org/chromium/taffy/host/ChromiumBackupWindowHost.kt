// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.content.ContentResolver
import android.net.Uri
import com.taffygo.browser.ui.app.AndroidBackupDocumentAdapter
import com.taffygo.browser.ui.app.AndroidBackupImportAdapter
import com.taffygo.browser.ui.app.BackupDeletionRequest
import com.taffygo.browser.ui.app.BackupDocumentCopy
import com.taffygo.browser.ui.app.BackupDocumentImport
import com.taffygo.browser.ui.app.BackupDocumentTransfer
import com.taffygo.browser.ui.app.BackupExportResult
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupRestoreCommitResult
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryRequest
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryResult
import com.taffygo.browser.ui.app.BackupRestorePreparation
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import com.taffygo.browser.ui.app.BackupRestoreReview
import com.taffygo.browser.ui.app.BackupRestoreStageResult
import com.taffygo.browser.ui.app.BackupWindowHost
import java.io.Closeable
import kotlinx.coroutines.CoroutineDispatcher
import org.chromium.base.ThreadUtils
import org.chromium.taffy.browser.TaffyBackupWorkflowBridge

/** Product adapter for one regular-profile window's trusted backup seam. */
class ChromiumBackupWindowHost(
    resolver: ContentResolver,
    ioDispatcher: CoroutineDispatcher,
    native: BackupNativeWindow?,
) : BackupWindowHost, Closeable {
    private val state = ChromiumBackupWindowState(native)
    private val restore = ChromiumBackupRestoreController(state)
    private val documentAdapter = AndroidBackupDocumentAdapter(
        resolver,
        ioDispatcher,
        object : AndroidBackupDocumentAdapter.NativeStaging {
            override fun openEncryptedArchiveReadFd(operationId: String) =
                state.native?.openEncryptedArchiveReadFd(operationId) ?: -1

            override fun openReadbackWriteFd(operationId: String, expectedArchiveBytes: Long) =
                state.native?.openReadbackWriteFd(operationId, expectedArchiveBytes) ?: -1

            override fun verifyEncryptedReadback(operationId: String) = when (
                state.native?.verifyEncryptedReadback(operationId)
            ) {
                TaffyBackupWorkflowBridge.ACCEPTED -> BackupDocumentTransfer.ReadbackStatus.VERIFIED
                TaffyBackupWorkflowBridge.REFUSED -> BackupDocumentTransfer.ReadbackStatus.REFUSED
                else -> BackupDocumentTransfer.ReadbackStatus.UNAVAILABLE
            }

            override fun abandonBackupStage(operationId: String) {
                val owned = synchronized(state.lock) {
                    state.sessions.firstOrNull { it.operationId == operationId }
                }
                // Stream outcome and native cleanup are separate facts. A failed
                // withdrawal remains retryable but must not erase the copy result.
                owned?.cancel()
            }
        },
    )
    private val importAdapter = AndroidBackupImportAdapter(
        resolver,
        ioDispatcher,
        object : AndroidBackupImportAdapter.NativeImportStaging {
            override fun openEncryptedImportWriteFd(operationId: String, maxBytes: Long) =
                state.native?.openEncryptedImportWriteFd(operationId, maxBytes) ?: -1

            override fun inspectImportedArchive(operationId: String, actualBytes: Long) = when (
                state.native?.inspectImportedArchive(operationId, actualBytes)
            ) {
                TaffyBackupWorkflowBridge.ACCEPTED -> BackupDocumentImport.ImportStatus.VERIFIED
                TaffyBackupWorkflowBridge.REFUSED -> BackupDocumentImport.ImportStatus.REFUSED
                else -> BackupDocumentImport.ImportStatus.UNAVAILABLE
            }

            override fun abandonImportStage(operationId: String) = withdrawOperation(operationId)
        },
    )

    fun activate(): Boolean {
        // Activation runs outside the lock, so the window's native handle is
        // read under it once and carried in a local. The same handle then
        // takes the compensating deactivation, so a window closed during the
        // call cannot leave an activated one behind.
        val owner = synchronized(state.lock) {
            val native = state.native
            if (state.closed || native == null) return false
            if (state.active) return true
            native
        }
        val activated = owner.activate()
        synchronized(state.lock) {
            if (state.closed) {
                if (activated) owner.deactivate()
                return false
            }
            state.active = activated
            return activated
        }
    }

    fun deactivate() {
        val shouldDeactivate = synchronized(state.lock) {
            if (state.closed || !state.active) false else {
                state.active = false
                state.activityIncarnation = Any()
                true
            }
        }
        if (shouldDeactivate) state.native?.deactivate()
    }

    override fun openRecoveryKeySession(
        mode: BackupRecoveryKeySession.Mode,
    ): BackupRecoveryKeySession? {
        val operationId = synchronized(state.lock) {
            if (state.closed || !state.active || state.deletionRequests.isNotEmpty() ||
                state.recoveredRestoreClaims.isNotEmpty()
            ) return null
            state.native?.beginRecoveryKeySession(mode.toNative()) ?: return null
        }
        val session = ChromiumBackupSession(state, operationId, mode)
        val retained = synchronized(state.lock) {
            // The native call may reenter this host. A selection admitted in
            // that interval wins; withdraw the unopened UI key ceremony below.
            if (state.closed || !state.active || state.deletionRequests.isNotEmpty() ||
                state.recoveredRestoreClaims.isNotEmpty()
            ) false
            else state.sessions.add(session)
        }
        if (!retained) {
            session.cancel()
            return null
        }
        return session
    }

    override fun openBackupDeletionRequest(): BackupDeletionRequest? = synchronized(state.lock) {
        if (state.closed || !state.active || state.sessions.isNotEmpty() ||
            state.deletionRequests.isNotEmpty() || state.recoveredRestoreClaims.isNotEmpty()
        ) null
        else ChromiumBackupDeletionRequest(state).also(state.deletionRequests::add)
    }

    override suspend fun completeBackupDeletionSelection(
        request: BackupDeletionRequest,
        source: Uri,
    ): BackupDocumentCopy? {
        val owned = synchronized(state.lock) {
            (request as? ChromiumBackupDeletionRequest)?.takeIf {
                it in state.deletionRequests && it.claimLocked()
            }
        } ?: return null
        try {
            val name = documentAdapter.readDeletionDisplayName(source) {
                synchronized(state.lock) { !state.closed && owned in state.deletionRequests }
            } ?: return null
            return synchronized(state.lock) {
                if (state.closed || owned !in state.deletionRequests) null
                else ChromiumBackupDocumentCopy(state, documentAdapter, source, name).also(state.documentCopies::add)
            }
        } finally {
            owned.close()
        }
    }

    override fun prepareExport(
        session: BackupRecoveryKeySession,
        selection: Set<BackupWindowHost.ContentClass>,
        onResult: (BackupWindowHost.ExportPreparation) -> Unit,
    ) {
        val owned = session as? ChromiumBackupSession
        if (owned == null) {
            onResult(BackupWindowHost.ExportPreparation.Refused)
            return
        }
        val wireSelection = selection.map(BackupWindowHost.ContentClass::toWire).sorted().toIntArray()
        val valid = synchronized(state.lock) {
            if (state.closed || !state.active || owned !in state.sessions ||
                owned.phase != BackupSessionPhase.CREATE_CONFIRMED || selection.isEmpty()
            ) false else {
                owned.phase = BackupSessionPhase.EXPORT_PREPARING
                true
            }
        }
        if (!valid) {
            onResult(BackupWindowHost.ExportPreparation.Refused)
            return
        }
        val admitted = state.native?.prepareExport(owned.operationId, wireSelection) { result ->
            var abandon = false
            val projected = synchronized(state.lock) {
                if (owned !in state.sessions || owned.phase != BackupSessionPhase.EXPORT_PREPARING) {
                    abandon = true
                    BackupWindowHost.ExportPreparation.Unavailable
                } else if (result.status == TaffyBackupWorkflowBridge.PREPARED &&
                    result.archiveBytes > 0
                ) {
                    owned.phase = BackupSessionPhase.EXPORT_READY
                    owned.archiveBytes = result.archiveBytes
                    BackupWindowHost.ExportPreparation.Ready(result.archiveBytes)
                } else {
                    state.finishLocked(owned)
                    abandon = true
                    if (result.status == TaffyBackupWorkflowBridge.REFUSED) {
                        BackupWindowHost.ExportPreparation.Refused
                    } else BackupWindowHost.ExportPreparation.Unavailable
                }
            }
            if (abandon) owned.cancel()
            onResult(projected)
        } ?: false
        if (!admitted) {
            synchronized(state.lock) { state.finishLocked(owned) }
            owned.cancel()
            onResult(BackupWindowHost.ExportPreparation.Unavailable)
        }
    }

    override suspend fun writePreparedExport(
        session: BackupRecoveryKeySession,
        destination: Uri,
    ): BackupExportResult {
        val owned = synchronized(state.lock) {
            (session as? ChromiumBackupSession)?.takeIf {
                it in state.sessions && it.phase == BackupSessionPhase.EXPORT_READY &&
                    it.archiveBytes > 0
            }?.also { it.phase = BackupSessionPhase.EXPORT_COPYING }
        } ?: return BackupExportResult(BackupDocumentTransfer.WriteResult.UNAVAILABLE)
        val result = try {
            documentAdapter.writeAndVerify(owned.operationId, owned.archiveBytes, destination)
        } finally {
            // Retain the exact handle independently of set membership so cleanup
            // can finish even if the document adapter's first withdrawal failed.
            owned.cancel()
        }
        val copy = synchronized(state.lock) {
            if (state.closed || result == BackupDocumentTransfer.WriteResult.UNAVAILABLE) null
            else ChromiumBackupDocumentCopy(state, documentAdapter, destination).also(state.documentCopies::add)
        }
        return BackupExportResult(result, copy)
    }

    override suspend fun copyAndInspectImport(
        session: BackupRecoveryKeySession,
        source: Uri,
    ): BackupDocumentImport.ImportStatus {
        val owned = synchronized(state.lock) {
            (session as? ChromiumBackupSession)?.takeIf {
                it in state.sessions && it.phase == BackupSessionPhase.IMPORT_READY &&
                    it.maximumBytes > 0
            }?.also { it.phase = BackupSessionPhase.IMPORT_COPYING }
        } ?: return BackupDocumentImport.ImportStatus.UNAVAILABLE
        val status = try {
            importAdapter.copyAndInspect(owned.operationId, owned.maximumBytes, source)
        } catch (cancelled: kotlinx.coroutines.CancellationException) {
            synchronized(state.lock) { state.finishLocked(owned) }
            throw cancelled
        }
        return synchronized(state.lock) {
            if (owned !in state.sessions || owned.phase != BackupSessionPhase.IMPORT_COPYING) {
                BackupDocumentImport.ImportStatus.UNAVAILABLE
            } else if (status == BackupDocumentImport.ImportStatus.VERIFIED) {
                owned.phase = BackupSessionPhase.IMPORT_VERIFIED
                status
            } else {
                state.finishLocked(owned)
                status
            }
        }
    }

    override fun prepareImportedRestore(
        session: BackupRecoveryKeySession,
        targetProfileLabel: String,
        onResult: (BackupRestorePreparation) -> Unit,
    ) = restore.prepare(session, targetProfileLabel, onResult)

    override fun confirmAndStageImportedRestore(
        review: BackupRestoreReview,
        onResult: (BackupRestoreStageResult) -> Unit,
    ) = restore.stage(review, onResult)

    override fun commitStagedImportedRestore(
        review: BackupRestoreReview,
        onResult: (BackupRestoreCommitResult) -> Unit,
    ) = restore.commit(review, onResult)

    override fun resolveImportedRestore(
        review: BackupRestoreReview,
        choice: BackupRestoreResolutionChoice,
        onResult: (BackupRestoreResolutionResult) -> Unit,
    ) = restore.resolve(review, choice, onResult)

    override fun discoverInterruptedRestore(
        onResult: (BackupRestoreDiscoveryResult) -> Unit,
    ): BackupRestoreDiscoveryRequest? = restore.discoverInterruptedRestore(onResult)

    override fun abandonImportedRestoreReview(review: BackupRestoreReview) =
        restore.abandonRecoveredReview(review)

    override suspend fun abandon(session: BackupRecoveryKeySession) {
        val owned = synchronized(state.lock) {
            (session as? ChromiumBackupSession)?.takeIf { it in state.sessions }
                ?.also(state::closeLocked)
        } ?: return
        try {
            importAdapter.abandon(owned.operationId)
        } finally {
            synchronized(state.lock) { state.sessions.remove(owned) }
        }
    }

    override fun close() {
        ThreadUtils.assertOnUiThread()
        val (owner, copies) = synchronized(state.lock) {
            if (state.closed) return
            state.closed = true
            state.active = false
            state.sessions.forEach {
                state.closeLocked(it)
                it.handoffToOwnerCloseLocked()
            }
            state.sessions.clear()
            state.deletionRequests.clear()
            val copies = state.documentCopies.toList()
            state.documentCopies.clear()
            state.native to copies
        }
        // Erase every undispatched URI without holding the window lock across
        // a copy lock. Closing access never requests provider deletion.
        copies.forEach(ChromiumBackupDocumentCopy::close)
        restore.close()
        owner?.close()
    }

    private fun withdrawOperation(operationId: String) {
        val owned = synchronized(state.lock) {
            state.sessions.firstOrNull { it.operationId == operationId }
        }
        owned?.withdrawNative()
    }
}
