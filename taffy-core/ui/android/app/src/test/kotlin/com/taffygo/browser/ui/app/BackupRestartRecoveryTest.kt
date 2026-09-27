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
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Key-free review through the same choice ceremony; not evidence of physical restore recovery. */
@OptIn(ExperimentalCoroutinesApi::class)
class BackupRestartRecoveryTest {
    private val host = BackupUiTestHost()
    private val request = Request()
    private val states = mutableListOf<BackupRestoreUiState>()
    private val notices = mutableListOf<BackupUiState.Notice>()
    private lateinit var scope: CoroutineScope
    private lateinit var recovery: BackupRestartRecovery

    @Before fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
        scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
        host.discoveryRequest = request
        recovery = BackupRestartRecovery(host, scope, states::add, notices::add)
    }

    @After fun tearDown() {
        recovery.close()
        scope.cancel()
        Dispatchers.resetMain()
    }

    @Test fun `starting and closing discovery never opens a key or chooses an outcome`() {
        recovery.start()
        recovery.start()
        assertEquals(1, host.discoveryCallbacks.size)
        assertEquals(BackupRestoreUiState.Step.DISCOVERING, states.single().step)
        assertTrue(host.sessions.isEmpty())
        assertTrue(host.selections.isEmpty())
        recovery.close()
        assertTrue(request.closed)
        assertTrue(host.resolutionChoices.isEmpty())
    }

    @Test fun `inline ready closes the returned request without withdrawing its delivered review`() {
        val review = Review()
        host.duringDiscovery = { it(BackupRestoreDiscoveryResult.Ready(review, false)) }
        recovery.start()
        assertTrue(request.closed)
        assertEquals(BackupRestoreUiState.Step.CANDIDATE, states.last().step)
        assertTrue(host.reviewWithdrawals.isEmpty())
        recovery.close()
        assertEquals(listOf(review), host.reviewWithdrawals)
    }

    @Test fun `every no-choice terminal has its own notice and never opens review`() {
        val expected = mapOf(
            BackupRestoreDiscoveryResult.None to BackupUiState.Notice.RECOVERY_NONE,
            BackupRestoreDiscoveryResult.SourceUnavailable to BackupUiState.Notice.RECOVERY_SOURCE_UNAVAILABLE,
            BackupRestoreDiscoveryResult.Unavailable to BackupUiState.Notice.RECOVERY_UNAVAILABLE,
            BackupRestoreDiscoveryResult.AlreadyKept to BackupUiState.Notice.RECOVERY_ALREADY_KEPT,
            BackupRestoreDiscoveryResult.AlreadyDiscarded to BackupUiState.Notice.RECOVERY_ALREADY_DISCARDED,
        )
        for ((result, notice) in expected) {
            host.discoveryRequest = Request()
            val local = BackupRestartRecovery(host, scope, states::add, notices::add)
            local.start()
            host.discoveryCallbacks.last()(result)
            assertEquals(notice, notices.last())
            assertEquals(BackupRestoreUiState.Step.DISCOVERING, states.last().step)
            local.close()
        }
        assertTrue(host.resolutionChoices.isEmpty())
        assertTrue(host.reviewWithdrawals.isEmpty())
    }

    @Test fun `every unresolved reason carries no summary or choice`() {
        for (reason in BackupRestoreDiscoveryResult.Reason.entries) {
            host.discoveryRequest = Request()
            val local = BackupRestartRecovery(host, scope, states::add, notices::add)
            local.start()
            host.discoveryCallbacks.last()(BackupRestoreDiscoveryResult.RecoveryRequired(reason))
            assertEquals(reason, states.last().recoveryReason)
            assertEquals(null, states.last().summary)
            assertEquals(BackupRestoreUiState.Step.NEEDS_RECOVERY, states.last().step)
            local.onIntent(BackupIntent.AcceptRestore, true)
            local.onIntent(BackupIntent.RequestDiscardRestore, true)
            local.onIntent(BackupIntent.DiscardRestore, true)
            local.close()
        }
        assertTrue(notices.isEmpty())
        assertTrue(host.resolutionChoices.isEmpty())
    }

    @Test fun `closed discovery withdraws a late review without publishing it`() {
        recovery.start()
        recovery.close()
        val review = Review()
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.Ready(review, false))
        assertEquals(listOf(review), host.reviewWithdrawals)
        assertEquals(1, states.size)
        assertTrue(notices.isEmpty())
    }

    @Test fun `a cancelled scope neither starts discovery nor loses a late review`() {
        recovery.start()
        scope.cancel()
        val review = Review()
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.Ready(review, false))
        assertEquals(listOf(review), host.reviewWithdrawals)
        assertTrue(request.closed)
        assertEquals(1, states.size)
        val next = BackupRestartRecovery(host, scope, states::add, notices::add)
        next.start()
        assertEquals(1, host.discoveryCallbacks.size)
        next.close()
    }

    @Test fun `duplicate delivery preserves the adopted review and closes a foreign one`() {
        val review = discover()
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.Ready(review, true))
        assertFalse(states.last().cleanupOnly)
        assertTrue(host.reviewWithdrawals.isEmpty())
        val foreign = Review()
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.Ready(foreign, false))
        assertEquals(listOf(foreign), host.reviewWithdrawals)
        assertEquals(2, states.size)
    }

    @Test fun `summary failure withdraws the new native review instead of leaving it held`() {
        recovery.start()
        val review = object : BackupRestoreReview {
            override val summary: BackupRestoreSummary get() = error("unavailable review")
        }
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.Ready(review, false))
        assertEquals(listOf(review), host.reviewWithdrawals)
        assertEquals(listOf(BackupUiState.Notice.RECOVERY_UNAVAILABLE), notices)
        assertTrue(request.closed)
    }

    @Test fun `saved empty classes survive defensive presentation copying`() {
        val rows = mutableListOf(row(BackupWindowHost.ContentClass.ASSISTANT_CONFIGURATION, 0),
            row(BackupWindowHost.ContentClass.LIBRARY, 1))
        val review = Review(exampleSummary().copy(selectedClasses = rows))
        recovery.start()
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.Ready(review, false))
        rows.clear()
        assertEquals(2, states.last().summary?.selectedClasses?.size)
        assertEquals(0, states.last().summary?.selectedClasses?.first()?.createCount)
        assertTrue(states.last().recovered)
    }

    @Test fun `recovered review cannot stage or commit and keep needs a resumed new click`() {
        val review = discover()
        act(BackupIntent.StageRestore)
        act(BackupIntent.CommitRestore)
        recovery.onIntent(BackupIntent.AcceptRestore, false)
        assertTrue(host.stageReviews.isEmpty())
        assertTrue(host.commitReviews.isEmpty())
        assertTrue(host.resolutionChoices.isEmpty())
        act(BackupIntent.AcceptRestore)
        assertEquals(listOf(review to BackupRestoreResolutionChoice.ACCEPT), host.resolutionChoices)
    }

    @Test fun `known noncommit offers only separately confirmed discard`() {
        val review = discover(cleanupOnly = true)
        act(BackupIntent.AcceptRestore)
        act(BackupIntent.DiscardRestore)
        assertTrue(host.resolutionChoices.isEmpty())
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.BackToRestoreReview)
        assertEquals(BackupRestoreUiState.Step.CLEANUP_REQUIRED, states.last().step)
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        assertEquals(listOf(review to BackupRestoreResolutionChoice.DISCARD), host.resolutionChoices)
    }

    @Test fun `definite noncompletion retains exact review for another explicit choice only`() {
        val review = discover()
        act(BackupIntent.AcceptRestore)
        host.resolutionCallbacks.single()(BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED)
        assertEquals(1, host.resolutionChoices.size)
        assertTrue(host.reviewWithdrawals.isEmpty())
        assertTrue(states.last().choiceNotCompleted)
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        assertSame(review, host.resolutionChoices.last().first)
        assertEquals(BackupRestoreResolutionChoice.DISCARD, host.resolutionChoices.last().second)
    }

    @Test fun `uncertain resolution blocks all repeated choices until fresh discovery`() {
        discover()
        act(BackupIntent.AcceptRestore)
        host.resolutionCallbacks.single()(BackupRestoreResolutionResult.RECOVERY_REQUIRED)
        act(BackupIntent.AcceptRestore)
        act(BackupIntent.RequestDiscardRestore)
        act(BackupIntent.DiscardRestore)
        assertEquals(BackupRestoreUiState.Step.NEEDS_RECOVERY, states.last().step)
        assertEquals(1, host.resolutionChoices.size)
        assertTrue(notices.isEmpty())
    }

    @Test fun `terminal keep releases review and closing during resolve ignores its late result`() {
        val review = discover()
        act(BackupIntent.AcceptRestore)
        host.resolutionCallbacks.single()(BackupRestoreResolutionResult.PUBLISHED)
        assertEquals(listOf(BackupUiState.Notice.RESTORE_KEPT), notices)
        assertEquals(listOf(review), host.reviewWithdrawals)

        val next = BackupRestartRecovery(host, scope, states::add, notices::add)
        next.start()
        val other = Review()
        host.discoveryCallbacks.last()(BackupRestoreDiscoveryResult.Ready(other, true))
        next.onIntent(BackupIntent.RequestDiscardRestore, true)
        next.onIntent(BackupIntent.DiscardRestore, true)
        next.close()
        host.resolutionCallbacks.last()(BackupRestoreResolutionResult.VERIFIED_DELETED)
        assertEquals(listOf(BackupUiState.Notice.RESTORE_KEPT), notices)
        assertEquals(listOf(review, other), host.reviewWithdrawals)
    }

    @Test fun `unavailable admission settles once and reentrant screen close starts no work`() {
        host.discoveryRequest = null
        recovery.start()
        assertEquals(listOf(BackupUiState.Notice.RECOVERY_UNAVAILABLE), notices)
        lateinit var next: BackupRestartRecovery
        next = BackupRestartRecovery(host, scope, { next.close() }, notices::add)
        next.start()
        assertEquals(1, host.discoveryCallbacks.size)
        assertEquals(1, notices.size)
        val cancelled = BackupRestartRecovery(host, scope, { scope.cancel() }, notices::add)
        cancelled.start()
        assertEquals(1, host.discoveryCallbacks.size)
        assertEquals(1, notices.size)
        cancelled.close()
    }

    private fun discover(cleanupOnly: Boolean = false): Review {
        recovery.start()
        val review = Review()
        host.discoveryCallbacks.last()(BackupRestoreDiscoveryResult.Ready(review, cleanupOnly))
        return review
    }

    private fun act(intent: BackupIntent) = recovery.onIntent(intent, true)

    private class Request : BackupRestoreDiscoveryRequest {
        var closed = false
        override fun close() { closed = true }
    }

    private class Review(override val summary: BackupRestoreSummary = exampleSummary()) : BackupRestoreReview

    private companion object {
        fun row(kind: BackupWindowHost.ContentClass, count: Int) = BackupRestoreClassSummary(kind, count, 0, 0, 0, 0, 0)
        fun exampleSummary() = BackupRestoreSummary("Restored profile", listOf(row(BackupWindowHost.ContentClass.LIBRARY, 1)), false, true)
    }
}
