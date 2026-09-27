// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.net.Uri
import com.taffygo.browser.ui.app.BackupDocumentImport
import com.taffygo.browser.ui.app.BackupDocumentCopy
import com.taffygo.browser.ui.app.BackupDocumentTransfer
import com.taffygo.browser.ui.app.BackupDocumentCleanupStep
import com.taffygo.browser.ui.app.BackupExportResult
import com.taffygo.browser.ui.app.BackupIntent
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupRestoreCommitResult
import com.taffygo.browser.ui.app.BackupRestorePreparation
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import com.taffygo.browser.ui.app.BackupRestoreReview
import com.taffygo.browser.ui.app.BackupRestoreStageResult
import com.taffygo.browser.ui.app.BackupUiState
import com.taffygo.browser.ui.app.BackupViewModel
import com.taffygo.browser.ui.app.BackupWindowHost
import com.taffygo.browser.ui.app.BackupDeletionRequest
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryRequest
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryResult
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.robolectric.shadows.ShadowLooper

/** Real Android URI/main-loop handling with a native port double; no archive crypto is faked as proof. */
@RunWith(BaseRobolectricTestRunner::class)
class BackupScreenTransferTest {
    @Test fun onlyVerifiedReadbackShowsSaved() {
        for (result in BackupDocumentTransfer.WriteResult.entries) {
            val fixture = Fixture()
            fixture.host.write = { result }
            fixture.export()
            val expected = when (result) {
                BackupDocumentTransfer.WriteResult.VERIFIED -> BackupUiState.Notice.SAVED
                BackupDocumentTransfer.WriteResult.INCOMPLETE -> BackupUiState.Notice.INCOMPLETE
                BackupDocumentTransfer.WriteResult.UNAVAILABLE -> BackupUiState.Notice.UNAVAILABLE
            }
            assertEquals(expected, fixture.model.state.value.notice)
            assertFalse(fixture.model.state.value.busy)
            assertTrue(fixture.host.key.cancelled)
        }
    }

    @Test fun checkedImportIsAbandonedAndNeverProjectsRestoredData() {
        val fixture = Fixture()
        fixture.checkImport()
        assertEquals(BackupUiState.Notice.CHECKED_ONLY, fixture.model.state.value.notice)
        assertEquals(1, fixture.host.abandoned)
        assertTrue(fixture.host.key.cancelled)
        assertEquals(0, fixture.host.writes)
    }

    @Test fun hostCancellationWithLiveParentSettlesBothDirections() {
        for (exporting in listOf(true, false)) {
            val fixture = Fixture()
            fixture.host.write = { throw CancellationException("test child timeout") }
            fixture.host.inspect = { throw CancellationException("test child timeout") }
            if (exporting) fixture.export() else fixture.checkImport()
            assertFalse(fixture.model.state.value.busy)
            assertEquals(
                if (exporting) BackupUiState.Notice.INCOMPLETE else BackupUiState.Notice.UNAVAILABLE,
                fixture.model.state.value.notice,
            )
            assertTrue(fixture.host.key.cancelled)
            assertEquals(1, fixture.host.abandoned)
        }
    }

    @Test fun cancelledWriteCannotPublishSuccessWhenItsProviderFinishesLater() {
        val fixture = Fixture()
        val held = CompletableDeferred<Unit>()
        fixture.host.write = { held.await(); BackupDocumentTransfer.WriteResult.VERIFIED }
        fixture.export()
        assertEquals(BackupUiState.Step.WRITING_EXPORT, fixture.model.state.value.step)
        fixture.model.onIntent(BackupIntent.Cancel)
        held.complete(Unit)
        ShadowLooper.idleMainLooper()
        assertEquals(BackupUiState.Notice.INCOMPLETE, fixture.model.state.value.notice)
        assertTrue(fixture.host.key.cancelled)
        assertEquals(1, fixture.host.abandoned)
    }

    @Test fun staleDocumentResultCannotWriteUsingANewerSession() {
        val fixture = Fixture()
        val old = fixture.request(exporting = true)
        fixture.model.onIntent(BackupIntent.Cancel)
        fixture.model.onIntent(BackupIntent.Check)
        val current = fixture.model.keySession()
        fixture.model.onDocumentResult(old, Uri.parse("content://backup.test/old"))
        ShadowLooper.idleMainLooper()
        assertSame(current, fixture.model.keySession())
        assertEquals(0, fixture.host.writes)
        assertEquals(0, fixture.host.imports)
        fixture.model.onHidden()
    }

    @Test fun repeatedDocumentCallbackStartsExactlyOneTransfer() {
        val fixture = Fixture()
        val held = CompletableDeferred<Unit>()
        fixture.host.inspect = { held.await(); BackupDocumentImport.ImportStatus.VERIFIED }
        val owned = fixture.request(exporting = false)
        val uri = Uri.parse("content://backup.test/one")
        fixture.model.onDocumentResult(owned, uri)
        fixture.model.onDocumentResult(owned, uri)
        ShadowLooper.idleMainLooper()
        assertEquals(1, fixture.host.imports)
        fixture.model.onHidden()
        held.complete(Unit)
        ShadowLooper.idleMainLooper()
        assertFalse(fixture.model.state.value.busy)
        assertTrue(fixture.host.key.cancelled)
    }

    @Test fun cleanupFailureStillWithdrawsTheExactSession() {
        for (failure in listOf(IllegalStateException("test unavailable"), CancellationException("test cleanup timeout"))) {
            val fixture = Fixture()
            fixture.host.cleanupFailure = failure
            fixture.checkImport()
            assertFalse(fixture.model.state.value.busy)
            assertTrue(fixture.host.key.cancelled)
            assertEquals(BackupUiState.Notice.CHECKED_ONLY, fixture.model.state.value.notice)
        }
    }

    @Test fun disposalDuringImportCleanupCannotSettleANewerOperation() {
        val fixture = Fixture()
        val held = CompletableDeferred<Unit>()
        fixture.host.cleanup = { held.await() }
        fixture.checkImport()
        fixture.model.onHidden()
        fixture.model.onIntent(BackupIntent.Check)
        val current = fixture.model.keySession()
        held.complete(Unit)
        ShadowLooper.idleMainLooper()
        assertSame(current, fixture.model.keySession())
        assertEquals(BackupUiState.Step.RECOVERY_KEY, fixture.model.state.value.step)
        fixture.model.onHidden()
    }

    @Test fun completedAndIncompleteExportOfferCleanupWithoutDeleting() {
        for (result in listOf(BackupDocumentTransfer.WriteResult.VERIFIED, BackupDocumentTransfer.WriteResult.INCOMPLETE)) {
            val fixture = Fixture()
            val copy = Copy()
            fixture.host.copy = copy
            fixture.host.write = { result }
            fixture.export()
            assertEquals(BackupDocumentCleanupStep.AVAILABLE, fixture.model.state.value.documentCleanup)
            assertEquals(0, copy.deletions)
            assertFalse(copy.closed)
            fixture.model.onHidden()
            assertTrue(copy.closed)
            assertEquals(0, copy.deletions)
        }
    }

    @Test fun anUnavailableExportCannotSupplyACleanupHandle() {
        val fixture = Fixture()
        val copy = Copy()
        fixture.host.copy = copy
        fixture.host.write = { BackupDocumentTransfer.WriteResult.UNAVAILABLE }
        fixture.export()
        assertEquals(null, fixture.model.state.value.documentCleanup)
        assertTrue(copy.closed)
        assertEquals(0, copy.deletions)
        assertEquals(BackupUiState.Notice.UNAVAILABLE, fixture.model.state.value.notice)
    }

    @Test fun cleanupConfirmationExcludesNewOperationsButDismissDoesNotDelete() {
        val fixture = Fixture()
        val copy = Copy()
        fixture.host.copy = copy
        fixture.export()
        fixture.model.onIntent(BackupIntent.RequestDeleteCopy)
        fixture.model.onIntent(BackupIntent.Create)
        fixture.model.onIntent(BackupIntent.Check)
        fixture.model.onIntent(BackupIntent.Restore)
        fixture.model.onIntent(BackupIntent.DismissNotice)
        assertEquals(BackupUiState.Step.IDLE, fixture.model.state.value.step)
        assertEquals(BackupDocumentCleanupStep.CONFIRMING, fixture.model.state.value.documentCleanup)
        assertTrue(fixture.model.state.value.busy)
        assertFalse(fixture.model.state.value.canCancel)
        assertEquals(0, copy.deletions)
        fixture.model.onIntent(BackupIntent.DeleteDocumentCopy)
        ShadowLooper.idleMainLooper()
        assertEquals(1, copy.deletions)
        assertEquals(BackupUiState.Notice.COPY_DELETED, fixture.model.state.value.notice)
        assertFalse(fixture.model.state.value.busy)
    }

    @Test fun aNewOperationForgetsThePreviousDocumentWithoutDeletingIt() {
        val fixture = Fixture()
        val copy = Copy()
        fixture.host.copy = copy
        fixture.export()
        fixture.model.onIntent(BackupIntent.Check)
        fixture.model.onIntent(BackupIntent.RequestDeleteCopy)
        fixture.model.onIntent(BackupIntent.DeleteDocumentCopy)
        assertTrue(copy.closed)
        assertEquals(0, copy.deletions)
        assertEquals(null, fixture.model.state.value.documentCleanup)
        assertEquals(BackupUiState.Step.RECOVERY_KEY, fixture.model.state.value.step)
        fixture.model.onHidden()
    }

    @Test fun lateExportHandleIsClosedInsteadOfAdoptedByANewOperation() {
        val fixture = Fixture()
        val held = CompletableDeferred<Unit>()
        val copy = Copy()
        fixture.host.copy = copy
        fixture.host.write = { withContext(NonCancellable) { held.await() }; BackupDocumentTransfer.WriteResult.VERIFIED }
        fixture.export()
        fixture.model.onHidden()
        fixture.model.onIntent(BackupIntent.Check)
        held.complete(Unit)
        ShadowLooper.idleMainLooper()
        assertTrue(copy.closed)
        assertEquals(0, copy.deletions)
        assertEquals(null, fixture.model.state.value.documentCleanup)
        assertEquals(BackupUiState.Step.RECOVERY_KEY, fixture.model.state.value.step)
        fixture.model.onHidden()
    }

    @Test fun hidingDuringDeleteLeavesUnknownAndLateSuccessCannotSettleNewWork() {
        val fixture = Fixture()
        val held = CompletableDeferred<Unit>()
        val copy = Copy().apply { delete = { withContext(NonCancellable) { held.await() }; BackupDocumentTransfer.DeleteResult.DELETED } }
        fixture.host.copy = copy
        fixture.export()
        fixture.model.onIntent(BackupIntent.RequestDeleteCopy)
        fixture.model.onIntent(BackupIntent.DeleteDocumentCopy)
        ShadowLooper.idleMainLooper()
        assertEquals(BackupDocumentCleanupStep.DELETING, fixture.model.state.value.documentCleanup)
        fixture.model.onHidden()
        assertEquals(BackupUiState.Notice.COPY_DELETE_UNVERIFIED, fixture.model.state.value.notice)
        fixture.model.onIntent(BackupIntent.Check)
        held.complete(Unit)
        ShadowLooper.idleMainLooper()
        assertTrue(copy.closed)
        assertEquals(BackupUiState.Step.RECOVERY_KEY, fixture.model.state.value.step)
        assertEquals(BackupUiState.Notice.NONE, fixture.model.state.value.notice)
        fixture.model.onHidden()
    }

    private class Copy : BackupDocumentCopy {
        var closed = false
        var deletions = 0
        var delete: suspend () -> BackupDocumentTransfer.DeleteResult = { BackupDocumentTransfer.DeleteResult.DELETED }
        override suspend fun deleteAndVerify(): BackupDocumentTransfer.DeleteResult {
            deletions++
            return delete()
        }
        override fun close() { closed = true }
    }

    private class Fixture {
        val host = Host()
        val model = BackupViewModel(host, mock(AnalyticsClient::class.java)).also { it.onResumed(true) }

        fun request(exporting: Boolean): BackupRecoveryKeySession {
            if (exporting) {
                model.onIntent(BackupIntent.Toggle(BackupWindowHost.ContentClass.LIBRARY))
                model.onIntent(BackupIntent.Create)
            } else model.onIntent(BackupIntent.Check)
            val owned = requireNotNull(model.keySession())
            model.onKeyOutcome(owned, BackupRecoveryKeySession.Outcome.CONFIRMED)
            ShadowLooper.idleMainLooper()
            return requireNotNull(model.claimDocumentRequest())
        }

        fun export() {
            model.onDocumentResult(request(true), Uri.parse("content://backup.test/export"))
            ShadowLooper.idleMainLooper()
        }

        fun checkImport() {
            model.onDocumentResult(request(false), Uri.parse("content://backup.test/import"))
            ShadowLooper.idleMainLooper()
        }
    }

    private class Host : BackupWindowHost {
        lateinit var key: Key
        var writes = 0
        var imports = 0
        var abandoned = 0
        var cleanupFailure: RuntimeException? = null
        var write: suspend () -> BackupDocumentTransfer.WriteResult = { BackupDocumentTransfer.WriteResult.VERIFIED }
        var inspect: suspend () -> BackupDocumentImport.ImportStatus = { BackupDocumentImport.ImportStatus.VERIFIED }
        var cleanup: suspend () -> Unit = {}
        var copy: BackupDocumentCopy? = null

        override fun openRecoveryKeySession(mode: BackupRecoveryKeySession.Mode): BackupRecoveryKeySession =
            Key(mode).also { key = it }

        override fun openBackupDeletionRequest(): BackupDeletionRequest? = null
        override fun discoverInterruptedRestore(onResult: (BackupRestoreDiscoveryResult) -> Unit): BackupRestoreDiscoveryRequest? = null
        override fun abandonImportedRestoreReview(review: BackupRestoreReview) = Unit
        override suspend fun completeBackupDeletionSelection(request: BackupDeletionRequest, source: Uri): BackupDocumentCopy? = null

        override fun prepareExport(
            session: BackupRecoveryKeySession,
            selection: Set<BackupWindowHost.ContentClass>,
            onResult: (BackupWindowHost.ExportPreparation) -> Unit,
        ) { onResult(BackupWindowHost.ExportPreparation.Ready(700)) }

        override suspend fun writePreparedExport(session: BackupRecoveryKeySession, destination: Uri): BackupExportResult {
            writes++
            return BackupExportResult(write(), copy)
        }

        override suspend fun copyAndInspectImport(session: BackupRecoveryKeySession, source: Uri): BackupDocumentImport.ImportStatus {
            imports++
            return inspect()
        }

        override suspend fun abandon(session: BackupRecoveryKeySession) {
            abandoned++
            cleanup()
            cleanupFailure?.let { throw it }
            session.cancel()
        }

        override fun prepareImportedRestore(session: BackupRecoveryKeySession, targetProfileLabel: String, onResult: (BackupRestorePreparation) -> Unit) {
            error("Check-only import must not prepare a restore")
        }

        override fun confirmAndStageImportedRestore(review: BackupRestoreReview, onResult: (BackupRestoreStageResult) -> Unit) {
            error("Check-only import must not stage a restore")
        }

        override fun commitStagedImportedRestore(review: BackupRestoreReview, onResult: (BackupRestoreCommitResult) -> Unit) {
            error("Check-only import must not commit a restore")
        }

        override fun resolveImportedRestore(review: BackupRestoreReview, choice: BackupRestoreResolutionChoice, onResult: (BackupRestoreResolutionResult) -> Unit) {
            error("Check-only import must not resolve a restore")
        }
    }

    private class Key(override val mode: BackupRecoveryKeySession.Mode) : BackupRecoveryKeySession {
        var cancelled = false
        override fun takeGeneratedKeyForDisplay(): CharArray? = null
        override fun confirmKeyRetained() = BackupRecoveryKeySession.Acceptance.ACCEPTED
        override fun acceptEnteredKey(text: CharArray) = BackupRecoveryKeySession.Acceptance.ACCEPTED
        override fun cancel() { cancelled = true }
    }
}
