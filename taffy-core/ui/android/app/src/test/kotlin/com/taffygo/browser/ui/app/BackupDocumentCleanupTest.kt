// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import kotlinx.coroutines.withContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class BackupDocumentCleanupTest {
    private val copy = Copy()
    private lateinit var scope: CoroutineScope
    private lateinit var cleanup: BackupDocumentCleanup
    private val states = mutableListOf<BackupDocumentCleanupStep>()
    private val notices = mutableListOf<BackupUiState.Notice>()

    @Before fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
        scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
        cleanup = BackupDocumentCleanup(copy, scope, states::add, notices::add)
    }

    @After fun tearDown() {
        cleanup.close()
        scope.cancel()
        Dispatchers.resetMain()
    }

    @Test fun `deletion needs a separate confirmation and keep cancels that choice`() {
        act(BackupIntent.DeleteDocumentCopy)
        assertEquals(0, copy.deletions)
        act(BackupIntent.RequestDeleteCopy)
        assertEquals(BackupDocumentCleanupStep.CONFIRMING, states.last())
        act(BackupIntent.KeepDocumentCopy)
        act(BackupIntent.DeleteDocumentCopy)
        assertEquals(BackupDocumentCleanupStep.AVAILABLE, states.last())
        assertEquals(0, copy.deletions)
        assertFalse(copy.closed)
    }

    @Test fun `paused confirmation cannot delete`() {
        cleanup.onIntent(BackupIntent.RequestDeleteCopy, false)
        assertTrue(states.isEmpty())
        act(BackupIntent.RequestDeleteCopy)
        cleanup.onIntent(BackupIntent.DeleteDocumentCopy, false)
        assertEquals(0, copy.deletions)
        assertTrue(notices.isEmpty())
    }

    @Test fun `duplicate clicks spend one attempt and successful deletion is terminal`() {
        val held = CompletableDeferred<BackupDocumentTransfer.DeleteResult>()
        copy.delete = { held.await() }
        confirm()
        act(BackupIntent.DeleteDocumentCopy)
        act(BackupIntent.RequestDeleteCopy)
        act(BackupIntent.KeepDocumentCopy)
        assertEquals(1, copy.deletions)
        assertEquals(BackupDocumentCleanupStep.DELETING, states.last())
        held.complete(BackupDocumentTransfer.DeleteResult.DELETED)
        confirm()
        assertEquals(listOf(BackupUiState.Notice.COPY_DELETED), notices)
        assertEquals(1, copy.deletions)
        assertTrue(copy.closed)
    }

    @Test fun `still present is not projected as deleted or retried`() {
        copy.delete = { BackupDocumentTransfer.DeleteResult.STILL_PRESENT }
        confirm()
        confirm()
        assertEquals(listOf(BackupUiState.Notice.COPY_STILL_PRESENT), notices)
        assertEquals(1, copy.deletions)
    }

    @Test fun `unverifiable is not projected as deleted or retried`() {
        copy.delete = { BackupDocumentTransfer.DeleteResult.UNVERIFIABLE }
        confirm()
        confirm()
        assertEquals(listOf(BackupUiState.Notice.COPY_DELETE_UNVERIFIED), notices)
        assertEquals(1, copy.deletions)
    }

    @Test fun `provider exception reports unknown and closes the exact copy`() {
        copy.delete = { throw IllegalStateException("provider unavailable") }
        confirm()
        assertEquals(listOf(BackupUiState.Notice.COPY_DELETE_UNVERIFIED), notices)
        assertTrue(copy.closed)
    }

    @Test fun `provider child cancellation with a live parent is unknown`() {
        copy.delete = { throw CancellationException("provider timeout") }
        confirm()
        assertEquals(listOf(BackupUiState.Notice.COPY_DELETE_UNVERIFIED), notices)
        assertTrue(copy.closed)
    }

    @Test fun `closing a confirmation never invokes document deletion`() {
        act(BackupIntent.RequestDeleteCopy)
        cleanup.close()
        confirm()
        assertEquals(0, copy.deletions)
        assertTrue(copy.closed)
        assertTrue(notices.isEmpty())
    }

    @Test fun `late provider success after closure cannot publish`() {
        val held = CompletableDeferred<BackupDocumentTransfer.DeleteResult>()
        copy.delete = { withContext(NonCancellable) { held.await() } }
        confirm()
        cleanup.close()
        held.complete(BackupDocumentTransfer.DeleteResult.DELETED)
        assertTrue(copy.closed)
        assertTrue(notices.isEmpty())
        assertEquals(1, copy.deletions)
    }

    private fun act(intent: BackupIntent) = cleanup.onIntent(intent, true)

    private fun confirm() {
        act(BackupIntent.RequestDeleteCopy)
        act(BackupIntent.DeleteDocumentCopy)
    }

    private class Copy : BackupDocumentCopy {
        var deletions = 0
        var closed = false
        var delete: suspend () -> BackupDocumentTransfer.DeleteResult = { BackupDocumentTransfer.DeleteResult.DELETED }
        override suspend fun deleteAndVerify(): BackupDocumentTransfer.DeleteResult {
            deletions++
            return delete()
        }
        override fun close() { closed = true }
    }
}
