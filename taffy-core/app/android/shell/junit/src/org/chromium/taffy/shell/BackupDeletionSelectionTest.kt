// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.net.Uri
import com.taffygo.browser.ui.app.BackupDeletionRequest
import com.taffygo.browser.ui.app.BackupDocumentCleanupStep
import com.taffygo.browser.ui.app.BackupDocumentCopy
import com.taffygo.browser.ui.app.BackupDocumentTransfer
import com.taffygo.browser.ui.app.BackupIntent
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupUiState
import com.taffygo.browser.ui.app.BackupViewModel
import com.taffygo.browser.ui.app.BackupWindowHost
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.mockito.Mockito.verifyNoInteractions
import org.robolectric.shadows.ShadowLooper

/** Metadata and lifetime ceremony only; document provider and archive authentication are not run. */
@RunWith(BaseRobolectricTestRunner::class)
class BackupDeletionSelectionTest {
    @Test fun selectionNeedsAResumedIdleWindowButNoKeyOrContentSelection() {
        val fixture = Fixture()
        fixture.model.onResumed(false)
        fixture.choose()
        assertTrue(fixture.host.requests.isEmpty())
        fixture.model.onResumed(true)
        fixture.choose()
        fixture.choose()
        fixture.model.onIntent(BackupIntent.Check)
        assertEquals(1, fixture.host.requests.size)
        assertEquals(0, fixture.host.keyCalls)
        assertNull(fixture.model.keySession())
        assertNull(fixture.model.claimDocumentRequest())
        assertTrue(fixture.model.state.value.selection.isEmpty())
        fixture.close()
    }

    @Test fun selectedNameIsPresentationOnlyAndDeletionNeedsAFreshConfirmation() {
        val fixture = Fixture()
        fixture.select()
        assertEquals("Older backup.taffybackup", fixture.model.state.value.documentName)
        assertEquals(BackupUiState.CleanupSource.SELECTED, fixture.model.state.value.cleanupSource)
        assertEquals(BackupDocumentCleanupStep.AVAILABLE, fixture.model.state.value.documentCleanup)
        assertEquals(0, fixture.host.copy.deletions)
        fixture.model.onIntent(BackupIntent.DeleteDocumentCopy)
        assertEquals(0, fixture.host.copy.deletions)
        fixture.model.onIntent(BackupIntent.RequestDeleteCopy)
        fixture.model.onIntent(BackupIntent.Check)
        assertEquals(0, fixture.host.keyCalls)
        fixture.model.onIntent(BackupIntent.KeepDocumentCopy)
        assertEquals(0, fixture.host.copy.deletions)
        fixture.confirm()
        assertEquals(1, fixture.host.copy.deletions)
        assertEquals(BackupUiState.Notice.COPY_DELETED, fixture.model.state.value.notice)
        assertNull(fixture.model.state.value.documentName)
        verifyNoInteractions(fixture.host.otherOperations)
        fixture.close()
    }

    @Test fun aClaimAndResultAreBothSingleUse() {
        val fixture = Fixture()
        val request = fixture.request()
        assertNull(fixture.model.claimDeletionDocumentRequest())
        fixture.model.onDeletionDocumentResult(request, fixture.uri)
        fixture.model.onDeletionDocumentResult(request, fixture.uri)
        fixture.idle()
        assertEquals(listOf(request to fixture.uri), fixture.host.reads)
        assertTrue(request.closed)
        assertEquals(0, fixture.host.copy.deletions)
        fixture.close()
    }

    @Test fun cancelledPickerClosesRequestWithoutLookingAtAnyFile() {
        val fixture = Fixture()
        val request = fixture.request()
        fixture.model.onDeletionDocumentResult(request, null)
        fixture.idle()
        assertTrue(request.closed)
        assertTrue(fixture.host.reads.isEmpty())
        assertEquals(BackupUiState.Notice.DELETE_SELECTION_CANCELLED, fixture.model.state.value.notice)
        assertFalse(fixture.model.state.value.busy)
        fixture.close()
    }

    @Test fun staleRequestAndPickerFailureCannotSettleANewerSelection() {
        val fixture = Fixture()
        val old = fixture.request()
        fixture.model.onIntent(BackupIntent.Cancel)
        val current = fixture.request()
        fixture.model.onDeletionDocumentResult(old, fixture.uri)
        fixture.model.onDeletionPickerUnavailable(old)
        assertEquals(BackupUiState.Step.WAITING_DELETE, fixture.model.state.value.step)
        assertTrue(fixture.host.reads.isEmpty())
        fixture.model.onDeletionDocumentResult(current, fixture.uri)
        fixture.idle()
        assertSame(current, fixture.host.reads.single().first)
        fixture.close()
    }

    @Test fun aLateMetadataHandleIsClosedAndCannotAttachToANewerKeyOperation() {
        val fixture = Fixture()
        val release = CompletableDeferred<Unit>()
        fixture.host.read = { withContext(NonCancellable) { release.await() }; fixture.host.copy }
        val request = fixture.request()
        fixture.model.onDeletionDocumentResult(request, fixture.uri)
        fixture.idle()
        assertEquals(BackupUiState.Step.PREPARING_DELETE, fixture.model.state.value.step)
        fixture.model.onHidden()
        fixture.model.onIntent(BackupIntent.Check)
        val key = fixture.model.keySession()
        release.complete(Unit)
        fixture.idle()
        assertTrue(request.closed)
        assertTrue(fixture.host.copy.closed)
        assertEquals(0, fixture.host.copy.deletions)
        assertSame(key, fixture.model.keySession())
        assertNull(fixture.model.state.value.documentCleanup)
        assertEquals(BackupUiState.Notice.NONE, fixture.model.state.value.notice)
        fixture.close()
    }

    @Test fun pickerMayFinishPausedButADeletionCannotBeConfirmedThere() {
        val fixture = Fixture()
        val request = fixture.request()
        fixture.model.onResumed(false)
        fixture.model.onDeletionDocumentResult(request, fixture.uri)
        fixture.idle()
        assertEquals(BackupDocumentCleanupStep.AVAILABLE, fixture.model.state.value.documentCleanup)
        fixture.confirm()
        assertEquals(0, fixture.host.copy.deletions)
        fixture.model.onResumed(true)
        fixture.confirm()
        assertEquals(1, fixture.host.copy.deletions)
        fixture.close()
    }

    @Test fun unlaunchedSelectionIsWithdrawnOnPauseAndNewWorkForgetsASelectedCopy() {
        val fixture = Fixture()
        fixture.choose()
        val old = fixture.host.requests.single()
        fixture.model.onResumed(false)
        assertTrue(old.closed)
        assertNull(fixture.model.claimDeletionDocumentRequest())
        fixture.model.onResumed(true)
        fixture.select()
        fixture.model.onIntent(BackupIntent.Check)
        assertTrue(fixture.host.copy.closed)
        assertNull(fixture.model.state.value.documentName)
        assertNull(fixture.model.state.value.documentCleanup)
        assertEquals(0, fixture.host.copy.deletions)
        fixture.close()
    }

    @Test fun missingMisleadingOrThrowingNamesNeverOfferDeletion() {
        for (name in listOf(null, "", "  ", "backup\nother", "backup\u202Ecod", "a".repeat(256))) {
            val fixture = Fixture()
            fixture.host.copy.name = { name }
            fixture.select()
            assertTrue(fixture.host.copy.closed)
            assertNull(fixture.model.state.value.documentCleanup)
            assertEquals(BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE, fixture.model.state.value.notice)
            fixture.close()
        }
        val fixture = Fixture()
        fixture.host.copy.name = { error("metadata unavailable") }
        fixture.select()
        assertTrue(fixture.host.copy.closed)
        assertNull(fixture.model.state.value.documentCleanup)
        fixture.close()
    }

    @Test fun selectionRefusalsAndLiveParentCancellationSettleWithoutDeleting() {
        for (failure in listOf(IllegalStateException("metadata unavailable"), CancellationException("child timeout"))) {
            val fixture = Fixture()
            fixture.host.read = { throw failure }
            fixture.select()
            assertTrue(fixture.host.requests.single().closed)
            assertFalse(fixture.model.state.value.busy)
            assertEquals(BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE, fixture.model.state.value.notice)
            assertEquals(0, fixture.host.copy.deletions)
            fixture.close()
        }
        val fixture = Fixture()
        fixture.host.available = false
        fixture.choose()
        assertEquals(BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE, fixture.model.state.value.notice)
        assertFalse(fixture.model.state.value.busy)
        fixture.host.available = true
        val request = fixture.request()
        fixture.model.onDeletionPickerUnavailable(request)
        assertTrue(request.closed)
        assertTrue(fixture.host.reads.isEmpty())
        fixture.close()
    }

    private class Request : BackupDeletionRequest {
        var closed = false
        override fun close() { closed = true }
    }

    private class Copy : BackupDocumentCopy {
        var name: () -> String? = { "Older backup.taffybackup" }
        override val displayName: String? get() = name()
        var closed = false
        var deletions = 0
        override suspend fun deleteAndVerify(): BackupDocumentTransfer.DeleteResult {
            deletions++
            return BackupDocumentTransfer.DeleteResult.DELETED
        }
        override fun close() { closed = true }
    }

    private class Host(
        val otherOperations: BackupWindowHost = mock(BackupWindowHost::class.java),
    ) : BackupWindowHost by otherOperations {
        var available = true
        var keyCalls = 0
        val copy = Copy()
        val requests = mutableListOf<Request>()
        val reads = mutableListOf<Pair<BackupDeletionRequest, Uri>>()
        var read: suspend () -> BackupDocumentCopy? = { copy }
        override fun openBackupDeletionRequest(): BackupDeletionRequest? =
            if (available) Request().also(requests::add) else null
        override suspend fun completeBackupDeletionSelection(request: BackupDeletionRequest, source: Uri): BackupDocumentCopy? {
            reads += request to source
            return read()
        }
        override fun openRecoveryKeySession(mode: BackupRecoveryKeySession.Mode): BackupRecoveryKeySession {
            keyCalls++
            return object : BackupRecoveryKeySession {
                override val mode = mode
                override fun takeGeneratedKeyForDisplay(): CharArray? = null
                override fun confirmKeyRetained() = BackupRecoveryKeySession.Acceptance.REFUSED
                override fun acceptEnteredKey(text: CharArray) = BackupRecoveryKeySession.Acceptance.REFUSED
                override fun cancel() = Unit
            }
        }
    }

    private class Fixture {
        val host = Host()
        val model = BackupViewModel(host, mock(AnalyticsClient::class.java)).also { it.onResumed(true) }
        val uri = Uri.parse("content://backup.test/selected")
        fun choose() { model.onIntent(BackupIntent.ChooseBackupForDeletion) }
        fun request(): Request {
            choose()
            return requireNotNull(model.claimDeletionDocumentRequest()) as Request
        }
        fun select() { model.onDeletionDocumentResult(request(), uri); idle() }
        fun confirm() {
            model.onIntent(BackupIntent.RequestDeleteCopy)
            model.onIntent(BackupIntent.DeleteDocumentCopy)
            idle()
        }
        fun idle() { ShadowLooper.idleMainLooper() }
        fun close() { model.onHidden(); idle() }
    }
}
