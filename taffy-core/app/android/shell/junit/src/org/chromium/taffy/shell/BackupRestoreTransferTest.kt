// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.net.Uri
import com.taffygo.browser.ui.app.BackupDocumentImport
import com.taffygo.browser.ui.app.BackupDocumentTransfer
import com.taffygo.browser.ui.app.BackupExportResult
import com.taffygo.browser.ui.app.BackupIntent
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupRestoreClassSummary
import com.taffygo.browser.ui.app.BackupRestoreCommitResult
import com.taffygo.browser.ui.app.BackupRestorePreparation
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import com.taffygo.browser.ui.app.BackupRestoreReview
import com.taffygo.browser.ui.app.BackupRestoreStageResult
import com.taffygo.browser.ui.app.BackupRestoreSummary
import com.taffygo.browser.ui.app.BackupRestoreUiState
import com.taffygo.browser.ui.app.BackupUiState
import com.taffygo.browser.ui.app.BackupViewModel
import com.taffygo.browser.ui.app.BackupWindowHost
import com.taffygo.browser.ui.app.BackupDeletionRequest
import com.taffygo.browser.ui.app.BackupDocumentCopy
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryRequest
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryResult
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.robolectric.shadows.ShadowLooper

/** Android transfer-to-review ownership, not evidence of native archive or restore correctness. */
@RunWith(BaseRobolectricTestRunner::class)
class BackupRestoreTransferTest {
    @Test fun verifiedRestoreImportRetainsItsStageUntilAnExplicitResumedReview() {
        val fixture = Fixture()
        val key = fixture.importBackup()
        assertEquals(BackupUiState.Step.RESTORE_IMPORTED, fixture.model.state.value.step)
        assertEquals(0, fixture.host.abandoned)
        assertFalse(key.cancelled)
        assertTrue(fixture.host.preparations.isEmpty())

        fixture.model.onResumed(false)
        fixture.act(BackupIntent.ReviewRestore("Restored profile"))
        assertTrue(fixture.host.preparations.isEmpty())
        fixture.model.onResumed(true)
        fixture.act(BackupIntent.ReviewRestore("Restored profile"))
        fixture.act(BackupIntent.ReviewRestore("Different label"))
        assertEquals(listOf(key to "Restored profile"), fixture.host.prepared)
        fixture.host.preparations.single()(BackupRestorePreparation.Ready(Review()))
        fixture.idle()
        assertEquals(BackupRestoreUiState.Step.REVIEW, fixture.model.state.value.restore?.step)
        fixture.model.onHidden()
    }

    @Test fun checkOnlyStillAbandonsVerifiedContentAndCannotStartRestoreReview() {
        val fixture = Fixture()
        val key = fixture.importBackup(checkOnly = true)
        assertEquals(1, fixture.host.abandoned)
        assertTrue(key.cancelled)
        assertEquals(BackupUiState.Notice.CHECKED_ONLY, fixture.model.state.value.notice)
        fixture.act(BackupIntent.ReviewRestore("Restored profile"))
        assertTrue(fixture.host.preparations.isEmpty())
    }

    @Test fun aDifferentTargetNameIsFrozenAtReviewAndClearedOnWithdrawal() {
        val fixture = Fixture()
        val key = fixture.importBackup()
        fixture.act(BackupIntent.RestoreNameChanged("Trip notes"))
        fixture.act(BackupIntent.ReviewRestore(" "))
        assertTrue(fixture.host.preparations.isEmpty())
        fixture.act(BackupIntent.ReviewRestore(requireNotNull(fixture.model.state.value.restoreName)))
        fixture.act(BackupIntent.RestoreNameChanged("Another name"))
        assertEquals(listOf(key to "Trip notes"), fixture.host.prepared)
        assertEquals("Trip notes", fixture.model.state.value.restoreName)
        fixture.model.onHidden()
        assertEquals(null, fixture.model.state.value.restoreName)
        fixture.request()
        assertEquals(null, fixture.model.state.value.restoreName)
        fixture.model.onHidden()
    }

    @Test fun failedRestoreImportNeverOffersReviewOrRetainsNativeCustody() {
        for (result in listOf(BackupDocumentImport.ImportStatus.REFUSED, BackupDocumentImport.ImportStatus.UNAVAILABLE)) {
            val fixture = Fixture()
            fixture.host.importResult = result
            val key = fixture.importBackup()
            assertTrue(key.cancelled)
            assertEquals(1, fixture.host.abandoned)
            assertEquals(BackupUiState.Step.IDLE, fixture.model.state.value.step)
            fixture.act(BackupIntent.ReviewRestore("Restored profile"))
            assertTrue(fixture.host.preparations.isEmpty())
        }
    }

    @Test fun hidingAnAuthenticatedImportWithdrawsItWithoutReassigningItsLateDocument() {
        val fixture = Fixture()
        val old = fixture.importBackup()
        fixture.model.onHidden()
        assertTrue(old.cancelled)
        assertEquals(BackupUiState.Notice.RESTORE_STOPPED, fixture.model.state.value.notice)
        val current = fixture.request()
        fixture.model.onDocumentResult(old, Uri.parse("content://backup.test/late-old"))
        fixture.idle()
        assertFalse(current.cancelled)
        assertEquals(BackupUiState.Step.WAITING_IMPORT, fixture.model.state.value.step)
        assertEquals(1, fixture.host.imports)
        fixture.model.onHidden()
    }

    @Test fun lateRestorePreviewCannotReplaceANewWindowsKeyCeremony() {
        val fixture = Fixture()
        val old = fixture.importBackup()
        fixture.act(BackupIntent.ReviewRestore("Restored profile"))
        fixture.model.onHidden()
        fixture.model.onResumed(true)
        fixture.act(BackupIntent.Restore)
        val current = fixture.model.keySession()
        fixture.host.preparations.single()(BackupRestorePreparation.Ready(Review()))
        fixture.idle()
        assertTrue(old.cancelled)
        assertSame(current, fixture.model.keySession())
        assertEquals(BackupUiState.Step.RECOVERY_KEY, fixture.model.state.value.step)
        assertEquals(null, fixture.model.state.value.restore)
        fixture.model.onHidden()
    }

    @Test fun commitCannotBeCancelledOrRepeatedButWindowLossWithdrawsOnlyItsPresentation() {
        val fixture = Fixture()
        val key = fixture.staged()
        fixture.act(BackupIntent.CommitRestore)
        fixture.act(BackupIntent.Cancel)
        fixture.act(BackupIntent.CommitRestore)
        assertFalse(key.cancelled)
        assertEquals(1, fixture.host.commits.size)
        assertEquals(BackupRestoreUiState.Step.COMMITTING, fixture.model.state.value.restore?.step)
        fixture.model.onHidden()
        assertTrue(key.cancelled)
        assertEquals(BackupUiState.Notice.RESTORE_NEEDS_REVIEW, fixture.model.state.value.notice)
        fixture.host.commits.single()(BackupRestoreCommitResult.HIDDEN_CANDIDATE)
        fixture.idle()
        assertEquals(null, fixture.model.state.value.restore)
        assertTrue(fixture.host.resolutions.isEmpty())
    }

    @Test fun keepingRequiresHiddenCandidateThenAnExactPublishedResult() {
        val fixture = Fixture()
        val key = fixture.staged()
        fixture.act(BackupIntent.CommitRestore)
        fixture.host.commits.single()(BackupRestoreCommitResult.HIDDEN_CANDIDATE)
        fixture.idle()
        assertFalse(key.cancelled)
        fixture.act(BackupIntent.AcceptRestore)
        assertEquals(listOf(BackupRestoreResolutionChoice.ACCEPT), fixture.host.choices)
        fixture.host.resolutions.single()(BackupRestoreResolutionResult.PUBLISHED)
        fixture.idle()
        assertEquals(BackupUiState.Notice.RESTORE_KEPT, fixture.model.state.value.notice)
        assertTrue(key.cancelled)
        assertEquals(null, fixture.model.state.value.restore)
    }

    @Test fun knownIncompleteChoiceRetainsItsSessionUntilAnotherExplicitChoice() {
        val fixture = Fixture()
        val key = fixture.staged()
        fixture.act(BackupIntent.CommitRestore)
        fixture.host.commits.single()(BackupRestoreCommitResult.HIDDEN_CANDIDATE)
        fixture.idle()
        fixture.act(BackupIntent.AcceptRestore)
        val oldReply = fixture.host.resolutions.single()
        oldReply(BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED)
        fixture.idle()
        assertFalse(key.cancelled)
        assertEquals(BackupRestoreUiState.Step.CANDIDATE, fixture.model.state.value.restore?.step)
        assertEquals(true, fixture.model.state.value.restore?.choiceNotCompleted)
        assertEquals(1, fixture.host.choices.size)

        fixture.act(BackupIntent.RequestDiscardRestore)
        fixture.act(BackupIntent.DiscardRestore)
        oldReply(BackupRestoreResolutionResult.PUBLISHED)
        fixture.idle()
        assertFalse(key.cancelled)
        assertEquals(BackupRestoreUiState.Step.RESOLVING, fixture.model.state.value.restore?.step)
        fixture.host.resolutions.last()(BackupRestoreResolutionResult.VERIFIED_DELETED)
        fixture.idle()
        assertTrue(key.cancelled)
        assertEquals(BackupUiState.Notice.RESTORE_DISCARDED, fixture.model.state.value.notice)
    }

    @Test fun knownNoncommitRetainsTheSessionForDiscardOnlyCleanup() {
        val fixture = Fixture()
        val key = fixture.staged()
        fixture.act(BackupIntent.CommitRestore)
        fixture.host.commits.single()(BackupRestoreCommitResult.DEFINITELY_NOT_COMMITTED)
        fixture.idle()
        assertFalse(key.cancelled)
        assertEquals(BackupRestoreUiState.Step.CLEANUP_REQUIRED, fixture.model.state.value.restore?.step)
        fixture.act(BackupIntent.AcceptRestore)
        fixture.act(BackupIntent.CommitRestore)
        fixture.act(BackupIntent.Cancel)
        assertTrue(fixture.host.choices.isEmpty())
        assertEquals(1, fixture.host.commits.size)
        assertFalse(key.cancelled)
        fixture.act(BackupIntent.RequestDiscardRestore)
        fixture.act(BackupIntent.DiscardRestore)
        assertEquals(listOf(BackupRestoreResolutionChoice.DISCARD), fixture.host.choices)
        fixture.host.resolutions.single()(BackupRestoreResolutionResult.VERIFIED_DELETED)
        fixture.idle()
        assertTrue(key.cancelled)
        assertEquals(BackupUiState.Notice.RESTORE_DISCARDED, fixture.model.state.value.notice)
    }

    @Test fun uncertainLiveCommitCanBeReviewedAgainWithoutReimportingOrReplayingCommit() {
        val fixture = Fixture()
        val key = fixture.staged()
        fixture.act(BackupIntent.CommitRestore)
        val oldCommit = fixture.host.commits.single()
        oldCommit(BackupRestoreCommitResult.RECOVERY_REQUIRED)
        fixture.idle()
        assertTrue(fixture.model.state.value.canCheckRecovery)
        assertFalse(fixture.model.state.value.canCheck)

        fixture.act(BackupIntent.ReviewInterruptedRestore)
        assertTrue(key.cancelled)
        assertEquals(null, fixture.model.keySession())
        assertEquals(BackupRestoreUiState.Step.DISCOVERING, fixture.model.state.value.restore?.step)
        val recovered = Review()
        fixture.host.discoveries.single()(BackupRestoreDiscoveryResult.Ready(recovered, false))
        fixture.idle()
        assertTrue(fixture.host.discoveryRequest.closed)
        assertTrue(fixture.host.withdrawnReviews.isEmpty())
        oldCommit(BackupRestoreCommitResult.DEFINITELY_NOT_COMMITTED)
        fixture.act(BackupIntent.StageRestore)
        fixture.act(BackupIntent.CommitRestore)
        assertEquals(BackupRestoreUiState.Step.CANDIDATE, fixture.model.state.value.restore?.step)
        assertEquals(true, fixture.model.state.value.restore?.recovered)
        assertEquals(1, fixture.host.keysOpened)
        assertEquals(1, fixture.host.imports)
        assertEquals(1, fixture.host.stages.size)
        assertEquals(1, fixture.host.commits.size)
        assertTrue(fixture.host.choices.isEmpty())

        fixture.act(BackupIntent.AcceptRestore)
        assertEquals(listOf(BackupRestoreResolutionChoice.ACCEPT), fixture.host.choices)
        fixture.host.resolutions.single()(BackupRestoreResolutionResult.PUBLISHED)
        fixture.idle()
        assertEquals(BackupUiState.Notice.RESTORE_KEPT, fixture.model.state.value.notice)
        assertEquals(listOf(recovered), fixture.host.withdrawnReviews)
        fixture.model.onHidden()
    }

    private class Fixture {
        val host = Host()
        val model = BackupViewModel(host, mock(AnalyticsClient::class.java))
        fun idle() = ShadowLooper.idleMainLooper()
        fun act(intent: BackupIntent) { model.onIntent(intent); idle() }

        fun request(checkOnly: Boolean = false): Key {
            model.onResumed(true)
            act(if (checkOnly) BackupIntent.Check else BackupIntent.Restore)
            val key = model.keySession() as Key
            model.onKeyOutcome(key, BackupRecoveryKeySession.Outcome.CONFIRMED)
            assertSame(key, model.claimDocumentRequest())
            return key
        }

        fun importBackup(checkOnly: Boolean = false): Key {
            val key = request(checkOnly)
            model.onDocumentResult(key, Uri.parse("content://backup.test/restore"))
            idle()
            return key
        }

        fun staged(): Key {
            val key = importBackup()
            act(BackupIntent.ReviewRestore("Restored profile"))
            host.preparations.single()(BackupRestorePreparation.Ready(Review()))
            idle()
            act(BackupIntent.StageRestore)
            host.stages.single()(BackupRestoreStageResult.STAGED)
            idle()
            return key
        }
    }

    private class Host : BackupWindowHost {
        var abandoned = 0
        var imports = 0
        var keysOpened = 0
        var importResult = BackupDocumentImport.ImportStatus.VERIFIED
        val prepared = mutableListOf<Pair<BackupRecoveryKeySession, String>>()
        val preparations = mutableListOf<(BackupRestorePreparation) -> Unit>()
        val stages = mutableListOf<(BackupRestoreStageResult) -> Unit>()
        val commits = mutableListOf<(BackupRestoreCommitResult) -> Unit>()
        val choices = mutableListOf<BackupRestoreResolutionChoice>()
        val resolutions = mutableListOf<(BackupRestoreResolutionResult) -> Unit>()
        val discoveries = mutableListOf<(BackupRestoreDiscoveryResult) -> Unit>()
        val discoveryRequest = DiscoveryRequest()
        val withdrawnReviews = mutableListOf<BackupRestoreReview>()

        override fun openRecoveryKeySession(mode: BackupRecoveryKeySession.Mode): Key { keysOpened++; return Key(mode) }
        override fun openBackupDeletionRequest(): BackupDeletionRequest? = null
        override fun discoverInterruptedRestore(onResult: (BackupRestoreDiscoveryResult) -> Unit): BackupRestoreDiscoveryRequest {
            discoveries += onResult
            return discoveryRequest
        }
        override fun abandonImportedRestoreReview(review: BackupRestoreReview) { withdrawnReviews += review }
        override suspend fun completeBackupDeletionSelection(request: BackupDeletionRequest, source: Uri): BackupDocumentCopy? = null
        override fun prepareExport(session: BackupRecoveryKeySession, selection: Set<BackupWindowHost.ContentClass>, onResult: (BackupWindowHost.ExportPreparation) -> Unit) = error("Not an export")
        override suspend fun writePreparedExport(session: BackupRecoveryKeySession, destination: Uri): BackupExportResult = error("Not an export")
        override suspend fun copyAndInspectImport(session: BackupRecoveryKeySession, source: Uri): BackupDocumentImport.ImportStatus {
            imports++
            return importResult
        }
        override suspend fun abandon(session: BackupRecoveryKeySession) { abandoned++; session.cancel() }
        override fun prepareImportedRestore(session: BackupRecoveryKeySession, targetProfileLabel: String, onResult: (BackupRestorePreparation) -> Unit) {
            prepared += session to targetProfileLabel
            preparations += onResult
        }
        override fun confirmAndStageImportedRestore(review: BackupRestoreReview, onResult: (BackupRestoreStageResult) -> Unit) { stages += onResult }
        override fun commitStagedImportedRestore(review: BackupRestoreReview, onResult: (BackupRestoreCommitResult) -> Unit) { commits += onResult }
        override fun resolveImportedRestore(review: BackupRestoreReview, choice: BackupRestoreResolutionChoice, onResult: (BackupRestoreResolutionResult) -> Unit) {
            choices += choice
            resolutions += onResult
        }
    }

    private class DiscoveryRequest : BackupRestoreDiscoveryRequest {
        var closed = false
        override fun close() { closed = true }
    }

    private class Key(override val mode: BackupRecoveryKeySession.Mode) : BackupRecoveryKeySession {
        var cancelled = false
        override fun takeGeneratedKeyForDisplay(): CharArray? = null
        override fun confirmKeyRetained() = BackupRecoveryKeySession.Acceptance.ACCEPTED
        override fun acceptEnteredKey(text: CharArray) = BackupRecoveryKeySession.Acceptance.ACCEPTED
        override fun cancel() { cancelled = true }
    }

    private class Review : BackupRestoreReview {
        override val summary = BackupRestoreSummary(
            "Restored profile", listOf(BackupRestoreClassSummary(BackupWindowHost.ContentClass.LIBRARY, 1, 0, 0, 0, 0, 0)), false, true,
        )
    }
}
