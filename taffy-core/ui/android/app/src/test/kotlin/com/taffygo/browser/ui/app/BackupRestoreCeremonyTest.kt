// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class BackupRestoreCeremonyTest {
    private val host = BackupUiTestHost()
    private val key = BackupUiTestHost.Key(BackupRecoveryKeySession.Mode.RESTORE)
    private lateinit var scope: CoroutineScope
    private lateinit var ceremony: BackupRestoreCeremony
    private val states = mutableListOf<BackupRestoreUiState>()
    private val notices = mutableListOf<BackupUiState.Notice>()
    private val state: BackupRestoreUiState get() = states.last()
    private var duringPublish: (BackupRestoreUiState) -> Unit = {}

    @Before fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
        scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
        ceremony = BackupRestoreCeremony(host, scope, { next ->
            states += next
            duringPublish(next)
        }, notices::add)
    }

    @After fun tearDown() {
        ceremony.close()
        scope.cancel()
        Dispatchers.resetMain()
    }

    @Test fun `review starts once with the original session and localized label`() {
        ceremony.start(key, "Restored profile")
        ceremony.start(key, "another name")
        assertEquals(listOf(key to "Restored profile"), host.restoreRequests)
        assertEquals(BackupRestoreUiState.Step.PLANNING, state.step)
        assertTrue(host.stageReviews.isEmpty())
        assertTrue(host.commitReviews.isEmpty())
    }

    @Test fun `closure during planning publication prevents preparation dispatch`() {
        duringPublish = { ceremony.close() }
        ceremony.start(key, "Restored profile")
        assertTrue(host.restoreRequests.isEmpty())
        assertTrue(notices.isEmpty())
    }

    @Test fun `closure during staging publication prevents stage dispatch`() {
        prepare()
        duringPublish = { if (it.step == BackupRestoreUiState.Step.STAGING) ceremony.close() }
        act(BackupIntent.StageRestore)
        assertTrue(host.stageReviews.isEmpty())
        assertTrue(notices.isEmpty())
    }

    @Test fun `closure during commit publication prevents consumptive dispatch`() {
        prepare()
        act(BackupIntent.StageRestore)
        host.stageCallbacks.single()(BackupRestoreStageResult.STAGED)
        duringPublish = { if (it.step == BackupRestoreUiState.Step.COMMITTING) ceremony.close() }
        act(BackupIntent.CommitRestore)
        assertTrue(host.commitReviews.isEmpty())
        assertTrue(notices.isEmpty())
    }

    @Test fun `closure during recovered resolution publication releases review without a choice`() {
        val recovered = review()
        ceremony.startRecovered(recovered, false)
        duringPublish = { if (it.step == BackupRestoreUiState.Step.RESOLVING) ceremony.close() }
        act(BackupIntent.AcceptRestore)
        assertTrue(host.resolutionChoices.isEmpty())
        assertEquals(listOf(recovered), host.reviewWithdrawals)
        assertTrue(notices.isEmpty())
    }

    @Test fun `scope loss during resolution publication prevents a new native choice`() {
        candidate()
        duringPublish = { if (it.step == BackupRestoreUiState.Step.RESOLVING) scope.cancel() }
        act(BackupIntent.AcceptRestore)
        assertTrue(host.resolutionChoices.isEmpty())
        assertTrue(notices.isEmpty())
    }

    @Test fun `confirmation staging and commit are three distinct explicit steps`() {
        val review = prepare()
        act(BackupIntent.CommitRestore)
        assertTrue(host.commitReviews.isEmpty())
        act(BackupIntent.StageRestore)
        act(BackupIntent.StageRestore)
        assertEquals(listOf(review), host.stageReviews)
        assertEquals(BackupRestoreUiState.Step.STAGING, state.step)
        assertTrue(host.commitReviews.isEmpty())
        host.stageCallbacks.single()(BackupRestoreStageResult.STAGED)
        assertEquals(BackupRestoreUiState.Step.STAGED, state.step)
        assertTrue(host.commitReviews.isEmpty())
        act(BackupIntent.CommitRestore)
        act(BackupIntent.CommitRestore)
        assertEquals(listOf(review), host.commitReviews)
        assertEquals(BackupRestoreUiState.Step.COMMITTING, state.step)
        assertFalse(state.mayCancelPreparation)
    }

    @Test fun `paused presentation cannot request staging commit or candidate resolution`() {
        prepare()
        ceremony.onIntent(BackupIntent.StageRestore, false)
        assertTrue(host.stageReviews.isEmpty())
        act(BackupIntent.StageRestore)
        host.stageCallbacks.single()(BackupRestoreStageResult.STAGED)
        ceremony.onIntent(BackupIntent.CommitRestore, false)
        assertTrue(host.commitReviews.isEmpty())
        act(BackupIntent.CommitRestore)
        host.commitCallbacks.single()(BackupRestoreCommitResult.HIDDEN_CANDIDATE)
        ceremony.onIntent(BackupIntent.AcceptRestore, false)
        ceremony.onIntent(BackupIntent.RequestDiscardRestore, false)
        assertTrue(host.resolutionChoices.isEmpty())
        assertEquals(BackupRestoreUiState.Step.CANDIDATE, state.step)
    }

    @Test fun `conflict or unsupported selection disables local confirmation`() {
        val review = review(canStage = false, hasConflicts = true)
        prepare(review)
        act(BackupIntent.StageRestore)
        assertTrue(host.stageReviews.isEmpty())
        assertEquals(false, state.summary?.canStage)
        assertEquals(true, state.summary?.hasConflicts)
    }

    @Test fun `empty selected classes remain visible and summary list cannot change underneath review`() {
        val rows = mutableListOf(row(0))
        val review = object : BackupRestoreReview {
            override val summary = BackupRestoreSummary("Restored profile", rows, false, true)
        }
        prepare(review)
        assertNotSame(rows, state.summary?.selectedClasses)
        rows.clear()
        assertEquals(listOf(row(0)), state.summary?.selectedClasses)
    }

    @Test fun `stale preparation and staging replies cannot replace the live reviewed handle`() {
        val owned = prepare()
        host.restoreCallbacks.single()(BackupRestorePreparation.Ready(review()))
        host.restoreCallbacks.single()(BackupRestorePreparation.Unavailable)
        act(BackupIntent.StageRestore)
        assertSame(owned, host.stageReviews.single())
        host.stageCallbacks.single()(BackupRestoreStageResult.STAGED)
        host.stageCallbacks.single()(BackupRestoreStageResult.REFUSED)
        assertEquals(BackupRestoreUiState.Step.STAGED, state.step)
        assertTrue(notices.isEmpty())
    }

    @Test fun `an unavailable summary ends preparation without retaining restore authority`() {
        val broken = object : BackupRestoreReview {
            override val summary: BackupRestoreSummary get() = error("Review unavailable")
        }
        prepare(broken)
        assertEquals(listOf(BackupUiState.Notice.UNAVAILABLE), notices)
        act(BackupIntent.StageRestore)
        act(BackupIntent.CommitRestore)
        assertTrue(host.stageReviews.isEmpty())
        assertTrue(host.commitReviews.isEmpty())
    }

    @Test fun `hidden candidate does not publish without an explicit keep choice`() {
        val review = candidate()
        assertTrue(host.resolutionChoices.isEmpty())
        assertTrue(notices.isEmpty())
        act(BackupIntent.AcceptRestore)
        act(BackupIntent.AcceptRestore)
        assertEquals(listOf(review to BackupRestoreResolutionChoice.ACCEPT), host.resolutionChoices)
        host.resolutionCallbacks.single()(BackupRestoreResolutionResult.PUBLISHED)
        assertEquals(listOf(BackupUiState.Notice.RESTORE_KEPT), notices)
    }

    @Test fun `discard needs its own confirmation and can return to review`() {
        val review = candidate()
        act(BackupIntent.DiscardRestore)
        assertTrue(host.resolutionChoices.isEmpty())
        act(BackupIntent.RequestDiscardRestore)
        assertEquals(BackupRestoreUiState.Step.CONFIRM_DISCARD, state.step)
        act(BackupIntent.BackToRestoreReview)
        assertEquals(BackupRestoreUiState.Step.CANDIDATE, state.step)
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        assertEquals(listOf(review to BackupRestoreResolutionChoice.DISCARD), host.resolutionChoices)
        host.resolutionCallbacks.single()(BackupRestoreResolutionResult.VERIFIED_DELETED)
        assertEquals(listOf(BackupUiState.Notice.RESTORE_DISCARDED), notices)
    }

    @Test fun `unknown commit cannot retry or resolve from UI state`() {
        prepare()
        act(BackupIntent.StageRestore)
        host.stageCallbacks.single()(BackupRestoreStageResult.STAGED)
        act(BackupIntent.CommitRestore)
        host.commitCallbacks.single()(BackupRestoreCommitResult.RECOVERY_REQUIRED)
        act(BackupIntent.CommitRestore)
        act(BackupIntent.AcceptRestore)
        act(BackupIntent.DiscardRestore)
        assertEquals(1, host.commitReviews.size)
        assertTrue(host.resolutionChoices.isEmpty())
        assertEquals(BackupRestoreUiState.Step.NEEDS_RECOVERY, state.step)
        assertFalse(state.mayCancelPreparation)
        assertTrue(notices.isEmpty())
    }

    @Test fun `known noncommit keeps only an explicit confirmed discard choice`() {
        val owned = prepare()
        act(BackupIntent.StageRestore)
        host.stageCallbacks.single()(BackupRestoreStageResult.STAGED)
        act(BackupIntent.CommitRestore)
        host.commitCallbacks.single()(BackupRestoreCommitResult.DEFINITELY_NOT_COMMITTED)
        assertEquals(BackupRestoreUiState.Step.CLEANUP_REQUIRED, state.step)
        assertTrue(state.cleanupOnly)
        assertTrue(notices.isEmpty())
        act(BackupIntent.AcceptRestore)
        act(BackupIntent.CommitRestore)
        act(BackupIntent.DiscardRestore)
        assertEquals(1, host.commitReviews.size)
        assertTrue(host.resolutionChoices.isEmpty())
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.BackToRestoreReview)
        assertEquals(BackupRestoreUiState.Step.CLEANUP_REQUIRED, state.step)
        act(BackupIntent.AcceptRestore)
        assertTrue(host.resolutionChoices.isEmpty())
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        assertEquals(listOf(owned to BackupRestoreResolutionChoice.DISCARD), host.resolutionChoices)
        host.resolutionCallbacks.last()(BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED)
        assertEquals(BackupRestoreUiState.Step.CLEANUP_REQUIRED, state.step)
        assertTrue(state.choiceNotCompleted)
        act(BackupIntent.AcceptRestore)
        assertEquals(1, host.resolutionChoices.size)
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        host.resolutionCallbacks.last()(BackupRestoreResolutionResult.VERIFIED_DELETED)
        assertEquals(listOf(BackupUiState.Notice.RESTORE_DISCARDED), notices)
    }

    @Test fun `mismatched resolution success stays unknown rather than changing the persons choice`() {
        candidate()
        act(BackupIntent.AcceptRestore)
        host.resolutionCallbacks.single()(BackupRestoreResolutionResult.VERIFIED_DELETED)
        assertEquals(BackupRestoreUiState.Step.NEEDS_RECOVERY, state.step)
        assertTrue(notices.isEmpty())
    }

    @Test fun `known incomplete choices retain the exact review but never retry automatically`() {
        val owned = candidate()
        for (result in listOf(
                BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED,
                BackupRestoreResolutionResult.REFUSED,
                BackupRestoreResolutionResult.UNAVAILABLE,
            )
        ) {
            act(BackupIntent.AcceptRestore)
            assertFalse(state.choiceNotCompleted)
            val dispatched = host.resolutionChoices.size
            host.resolutionCallbacks.last()(result)
            assertEquals(dispatched, host.resolutionChoices.size)
            assertSame(owned, host.resolutionChoices.last().first)
            assertEquals(BackupRestoreUiState.Step.CANDIDATE, state.step)
            assertTrue(state.choiceNotCompleted)
            assertTrue(notices.isEmpty())
        }
        // A new attempt still needs the person's exact choice, including a
        // separate discard confirmation rather than reusing the prior Keep.
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        assertEquals(BackupRestoreResolutionChoice.DISCARD, host.resolutionChoices.last().second)
        host.resolutionCallbacks.last()(BackupRestoreResolutionResult.VERIFIED_DELETED)
        assertEquals(listOf(BackupUiState.Notice.RESTORE_DISCARDED), notices)
    }

    @Test fun `an unknown resolution still withdraws both choices without claiming a definite result`() {
        candidate()
        act(BackupIntent.AcceptRestore)
        host.resolutionCallbacks.single()(BackupRestoreResolutionResult.RECOVERY_REQUIRED)
        act(BackupIntent.AcceptRestore)
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        assertEquals(1, host.resolutionChoices.size)
        assertEquals(BackupRestoreUiState.Step.NEEDS_RECOVERY, state.step)
        assertFalse(state.choiceNotCompleted)
        assertTrue(notices.isEmpty())
    }

    @Test fun `a late reply to an earlier choice cannot settle a later explicit choice`() {
        candidate()
        act(BackupIntent.AcceptRestore)
        val oldReply = host.resolutionCallbacks.single()
        oldReply(BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED)
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        oldReply(BackupRestoreResolutionResult.PUBLISHED)
        oldReply(BackupRestoreResolutionResult.RECOVERY_REQUIRED)
        assertEquals(BackupRestoreUiState.Step.RESOLVING, state.step)
        assertTrue(notices.isEmpty())
        host.resolutionCallbacks.last()(BackupRestoreResolutionResult.VERIFIED_DELETED)
        assertEquals(listOf(BackupUiState.Notice.RESTORE_DISCARDED), notices)
    }

    @Test fun `closing while planning withdraws publication and never restores a callback`() {
        ceremony.start(key, "Restored profile")
        ceremony.close()
        host.restoreCallbacks.single()(BackupRestorePreparation.Ready(review()))
        act(BackupIntent.StageRestore)
        assertEquals(1, states.size)
        assertTrue(host.stageReviews.isEmpty())
        assertTrue(notices.isEmpty())
    }

    @Test fun `closing after dispatch cannot turn a late physical result into UI success`() {
        candidate()
        act(BackupIntent.AcceptRestore)
        val count = states.size
        ceremony.close()
        host.resolutionCallbacks.single()(BackupRestoreResolutionResult.PUBLISHED)
        assertEquals(count, states.size)
        assertTrue(notices.isEmpty())
    }

    private fun act(intent: BackupIntent) = ceremony.onIntent(intent, true)

    private fun prepare(owned: BackupRestoreReview = review()): BackupRestoreReview {
        ceremony.start(key, "Restored profile")
        host.restoreCallbacks.single()(BackupRestorePreparation.Ready(owned))
        return owned
    }

    private fun candidate(): BackupRestoreReview {
        val owned = prepare()
        act(BackupIntent.StageRestore)
        host.stageCallbacks.single()(BackupRestoreStageResult.STAGED)
        act(BackupIntent.CommitRestore)
        host.commitCallbacks.single()(BackupRestoreCommitResult.HIDDEN_CANDIDATE)
        return owned
    }

    private fun review(canStage: Boolean = true, hasConflicts: Boolean = false) = object : BackupRestoreReview {
        override val summary = BackupRestoreSummary("Restored profile", listOf(row()), hasConflicts, canStage)
    }

    private fun row(createCount: Int = 2) = BackupRestoreClassSummary(
        BackupWindowHost.ContentClass.LIBRARY, createCount, 0, 0, 0, 0, 0,
    )
}
