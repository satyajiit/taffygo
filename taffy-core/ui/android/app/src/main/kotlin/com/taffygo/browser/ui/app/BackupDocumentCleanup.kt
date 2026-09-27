// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.annotation.MainThread
import java.io.Closeable
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch

/** A separate destructive choice, never a side effect of selection, export failure or dismissal. */
@MainThread
internal class BackupDocumentCleanup(
    copy: BackupDocumentCopy,
    private val scope: CoroutineScope,
    private var publish: (BackupDocumentCleanupStep) -> Unit,
    private var finish: (BackupUiState.Notice) -> Unit,
) : Closeable {
    private var copy: BackupDocumentCopy? = copy
    private var step = BackupDocumentCleanupStep.AVAILABLE
    private var job: Job? = null
    private var closed = false

    fun onIntent(intent: BackupIntent, resumed: Boolean) {
        if (closed || !resumed) return
        when (intent) {
            BackupIntent.RequestDeleteCopy -> if (step == BackupDocumentCleanupStep.AVAILABLE) {
                transition(BackupDocumentCleanupStep.CONFIRMING)
            }
            BackupIntent.KeepDocumentCopy -> if (step == BackupDocumentCleanupStep.CONFIRMING) {
                transition(BackupDocumentCleanupStep.AVAILABLE)
            }
            BackupIntent.DeleteDocumentCopy -> if (step == BackupDocumentCleanupStep.CONFIRMING) delete()
            else -> Unit
        }
    }

    private fun delete() {
        val owned = copy ?: return
        transition(BackupDocumentCleanupStep.DELETING)
        job = scope.launch(start = CoroutineStart.LAZY) {
            val result = try {
                owned.deleteAndVerify()
            } catch (_: CancellationException) {
                currentCoroutineContext().ensureActive()
                BackupDocumentTransfer.DeleteResult.UNVERIFIABLE
            } catch (_: RuntimeException) {
                BackupDocumentTransfer.DeleteResult.UNVERIFIABLE
            } finally {
                owned.close()
            }
            if (closed) return@launch
            val notice = when (result) {
                BackupDocumentTransfer.DeleteResult.DELETED -> BackupUiState.Notice.COPY_DELETED
                BackupDocumentTransfer.DeleteResult.STILL_PRESENT -> BackupUiState.Notice.COPY_STILL_PRESENT
                BackupDocumentTransfer.DeleteResult.UNVERIFIABLE -> BackupUiState.Notice.COPY_DELETE_UNVERIFIED
            }
            val completed = finish
            close()
            completed(notice)
        }
        job?.start()
    }

    private fun transition(next: BackupDocumentCleanupStep) {
        step = next
        publish(next)
    }

    override fun close() {
        if (closed) return
        closed = true
        publish = {}
        finish = {}
        job?.cancel()
        job = null
        copy?.close()
        copy = null
    }
}
