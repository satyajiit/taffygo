// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class BackupViewModelTest {
    private val host = BackupUiTestHost()
    private val model = host.viewModel()

    @Before fun setUp() { Dispatchers.setMain(UnconfinedTestDispatcher()) }
    @After fun tearDown() {
        model.onHidden()
        Dispatchers.resetMain()
    }

    @Test fun `a new or paused screen creates no native authority`() {
        model.onIntent(BackupIntent.Create)
        model.onIntent(BackupIntent.Check)
        assertTrue(host.sessions.isEmpty())
        assertTrue(model.state.value.selection.isEmpty())
    }

    @Test fun `export requires a selection but checking a file does not`() {
        model.onResumed(true)
        model.onIntent(BackupIntent.Create)
        assertTrue(host.sessions.isEmpty())
        model.onIntent(BackupIntent.Check)
        assertEquals(BackupRecoveryKeySession.Mode.RESTORE, model.keySession()?.mode)
        assertTrue(host.selections.isEmpty())
    }

    @Test fun `opening the key ceremony does not read a snapshot`() {
        beginCreate()
        assertTrue(host.selections.isEmpty())
        assertEquals(BackupUiState.Step.RECOVERY_KEY, model.state.value.step)
    }

    @Test fun `confirmed key reads only the frozen explicit selection`() {
        val key = beginCreate()
        model.onIntent(BackupIntent.Toggle(BackupWindowHost.ContentClass.MEMORY))
        model.onKeyOutcome(key, BackupRecoveryKeySession.Outcome.CONFIRMED)
        assertEquals(listOf(setOf(BackupWindowHost.ContentClass.LIBRARY)), host.selections)
        assertEquals(BackupUiState.Step.PREPARING_EXPORT, model.state.value.step)
        assertNull(model.keySession())
    }

    @Test fun `cancelled or unavailable key ceremony never prepares content`() {
        for (outcome in listOf(BackupRecoveryKeySession.Outcome.CANCELLED, BackupRecoveryKeySession.Outcome.UNAVAILABLE)) {
            val key = beginCreate()
            model.onKeyOutcome(key, outcome)
            assertTrue(key.cancelled)
            assertFalse(model.state.value.busy)
        }
        assertTrue(host.selections.isEmpty())
    }

    @Test fun `unavailable regular window is visible and receives no selection`() {
        host.available = false
        beginCreateSession()
        assertEquals(BackupUiState.Notice.UNAVAILABLE, model.state.value.notice)
        assertTrue(host.selections.isEmpty())
    }

    @Test fun `native refusal and invalid archive size cannot open a picker`() {
        for (result in listOf(BackupWindowHost.ExportPreparation.Refused, BackupWindowHost.ExportPreparation.Ready(0))) {
            val key = beginCreate()
            model.onKeyOutcome(key, BackupRecoveryKeySession.Outcome.CONFIRMED)
            host.callbacks.last()(result)
            assertNull(model.claimDocumentRequest())
            assertTrue(key.cancelled)
            assertFalse(model.state.value.busy)
        }
    }

    @Test fun `old preparation cannot advance or cancel a newer key ceremony`() {
        val old = beginCreate()
        model.onKeyOutcome(old, BackupRecoveryKeySession.Outcome.CONFIRMED)
        val callback = host.callbacks.single()
        model.onIntent(BackupIntent.Cancel)
        val current = beginCreate()
        callback(BackupWindowHost.ExportPreparation.Ready(800))
        callback(BackupWindowHost.ExportPreparation.Refused)
        assertSame(current, model.keySession())
        assertFalse(current.cancelled)
    }

    @Test fun `picker request is spent once and duplicate preparation is ignored`() {
        val key = prepareCreate()
        assertSame(key, model.claimDocumentRequest())
        assertNull(model.claimDocumentRequest())
        host.callbacks.single()(BackupWindowHost.ExportPreparation.Refused)
        assertEquals(BackupUiState.Step.WAITING_EXPORT, model.state.value.step)
        assertFalse(key.cancelled)
    }

    @Test fun `old key and picker results cannot cancel a newer session`() {
        val old = prepareCreate()
        model.claimDocumentRequest()
        model.onIntent(BackupIntent.Cancel)
        val current = beginCreate()
        model.onDocumentResult(old, null)
        model.onKeyOutcome(old, BackupRecoveryKeySession.Outcome.CANCELLED)
        model.onPickerUnavailable(old)
        assertSame(current, model.keySession())
        assertFalse(current.cancelled)
        assertEquals(0, host.documentCalls)
    }

    @Test fun `backgrounding a key or unlaunched preparation withdraws its operation`() {
        val key = beginCreate()
        model.onResumed(false)
        assertTrue(key.cancelled)
        val preparing = beginCreate()
        model.onKeyOutcome(preparing, BackupRecoveryKeySession.Outcome.CONFIRMED)
        model.onResumed(false)
        host.callbacks.single()(BackupWindowHost.ExportPreparation.Ready(800))
        assertTrue(preparing.cancelled)
        assertNull(model.claimDocumentRequest())
    }

    @Test fun `confirmed launched picker survives pause but not screen disposal`() {
        val key = prepareCreate()
        model.claimDocumentRequest()
        model.onResumed(false)
        assertFalse(key.cancelled)
        assertEquals(BackupUiState.Step.WAITING_EXPORT, model.state.value.step)
        model.onHidden()
        assertTrue(key.cancelled)
        assertFalse(model.state.value.busy)
        model.onDocumentResult(key, null)
        assertEquals(0, host.documentCalls)
    }

    @Test fun `cancelled picker never opens the document or leaves a key`() {
        val key = prepareCreate()
        model.claimDocumentRequest()
        model.onDocumentResult(key, null)
        assertTrue(key.cancelled)
        assertEquals(BackupUiState.Notice.CANCELLED, model.state.value.notice)
        assertEquals(0, host.documentCalls)
    }

    @Test fun `new window has no old operation or automatic retry`() {
        val old = prepareCreate()
        model.claimDocumentRequest()
        val fresh = host.viewModel()
        fresh.onDocumentResult(old, null)
        assertEquals(BackupUiState(), fresh.state.value)
        assertNull(fresh.keySession())
        assertEquals(1, host.sessions.size)
    }

    @Test fun `only the content free screen identifier is recorded`() {
        model.onShown()
        assertEquals(listOf(AnalyticsEvent.ScreenShown(TaffyDestination.Backup.screenId)), host.events)
    }

    private fun beginCreateSession() {
        model.onResumed(true)
        if (BackupWindowHost.ContentClass.LIBRARY !in model.state.value.selection) {
            model.onIntent(BackupIntent.Toggle(BackupWindowHost.ContentClass.LIBRARY))
        }
        model.onIntent(BackupIntent.Create)
    }

    private fun beginCreate(): BackupUiTestHost.Key {
        beginCreateSession()
        return host.sessions.last()
    }

    private fun prepareCreate(): BackupUiTestHost.Key = beginCreate().also {
        model.onKeyOutcome(it, BackupRecoveryKeySession.Outcome.CONFIRMED)
        host.callbacks.last()(BackupWindowHost.ExportPreparation.Ready(800))
    }
}
