// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class BackupRestartViewModelTest {
    private val host = BackupUiTestHost()
    private val request = Request()
    private val model = host.viewModel()

    @Before fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
        host.discoveryRequest = request
    }
    @After fun tearDown() { model.onHidden(); Dispatchers.resetMain() }

    @Test fun `only a resumed idle screen can begin key free discovery`() {
        model.onIntent(BackupIntent.ReviewInterruptedRestore)
        assertTrue(host.discoveryCallbacks.isEmpty())
        model.onResumed(true)
        model.onIntent(BackupIntent.ReviewInterruptedRestore)
        model.onIntent(BackupIntent.ReviewInterruptedRestore)
        model.onIntent(BackupIntent.Check)
        model.onIntent(BackupIntent.ChooseBackupForDeletion)
        assertEquals(1, host.discoveryCallbacks.size)
        assertTrue(host.sessions.isEmpty())
        assertNull(model.keySession())
        assertEquals(BackupUiState.Step.RECOVERING, model.state.value.step)
        assertTrue(model.state.value.canCancel)
    }

    @Test fun `pending discovery cancellation and a late review do not affect a new key ceremony`() {
        start()
        val old = host.discoveryCallbacks.single()
        model.onIntent(BackupIntent.Cancel)
        assertTrue(request.closed)
        assertEquals(BackupUiState.Notice.RECOVERY_CLOSED, model.state.value.notice)
        model.onIntent(BackupIntent.Check)
        val review = Review()
        old(BackupRestoreDiscoveryResult.Ready(review, false))
        assertEquals(listOf(review), host.reviewWithdrawals)
        assertEquals(BackupUiState.Step.RECOVERY_KEY, model.state.value.step)
        assertEquals(BackupUiState.Notice.NONE, model.state.value.notice)
        assertNull(model.state.value.restore)
    }

    @Test fun `paused observation may finish but no choice is dispatched until resumed`() {
        start()
        model.onResumed(false)
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.Ready(Review(), false))
        model.onIntent(BackupIntent.AcceptRestore)
        assertTrue(host.resolutionChoices.isEmpty())
        assertFalse(model.state.value.canCheckRecovery)
        model.onResumed(true)
        model.onIntent(BackupIntent.AcceptRestore)
        assertEquals(1, host.resolutionChoices.size)
        assertFalse(model.state.value.canCancel)
    }

    @Test fun `unresolved discovery permits a fresh check but not a fresh restore`() {
        start()
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.RecoveryRequired(
            BackupRestoreDiscoveryResult.Reason.SCHEMA_MISMATCH,
        ))
        assertTrue(model.state.value.canCheckRecovery)
        assertFalse(model.state.value.canCheck)
        assertNull(model.state.value.restore?.summary)
        model.onIntent(BackupIntent.Restore)
        assertTrue(host.sessions.isEmpty())
        model.onIntent(BackupIntent.ReviewInterruptedRestore)
        assertEquals(2, host.discoveryCallbacks.size)
        assertEquals(BackupRestoreUiState.Step.DISCOVERING, model.state.value.restore?.step)
    }

    @Test fun `known terminal ends review without inventing a new resolution choice`() {
        start()
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.AlreadyDiscarded)
        assertEquals(BackupUiState.Notice.RECOVERY_ALREADY_DISCARDED, model.state.value.notice)
        assertFalse(model.state.value.busy)
        assertNull(model.state.value.restore)
        assertTrue(host.resolutionChoices.isEmpty())
        assertTrue(request.closed)
    }

    @Test fun `screen closure releases recovered review and never asks for discard`() {
        start()
        val review = Review()
        host.discoveryCallbacks.single()(BackupRestoreDiscoveryResult.Ready(review, true))
        model.onIntent(BackupIntent.RequestDiscardRestore)
        model.onHidden()
        assertEquals(listOf(review), host.reviewWithdrawals)
        assertTrue(host.resolutionChoices.isEmpty())
        assertNull(model.state.value.restore)
        assertEquals(BackupUiState.Notice.RECOVERY_CLOSED, model.state.value.notice)
    }

    private fun start() {
        model.onResumed(true)
        model.onIntent(BackupIntent.ReviewInterruptedRestore)
    }

    private class Request : BackupRestoreDiscoveryRequest {
        var closed = false
        override fun close() { closed = true }
    }

    private class Review : BackupRestoreReview {
        override val summary = BackupRestoreSummary("Restored profile", listOf(
            BackupRestoreClassSummary(BackupWindowHost.ContentClass.LIBRARY, 0, 0, 0, 0, 0, 0),
        ), false, true)
    }
}
