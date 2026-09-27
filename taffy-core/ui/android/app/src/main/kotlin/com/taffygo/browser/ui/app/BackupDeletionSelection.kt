// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.net.Uri
import androidx.annotation.MainThread
import java.io.Closeable
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch

/** Owns one selection and its late result. Selecting never grants permission to delete. */
@MainThread
internal class BackupDeletionSelection(
    private val host: BackupWindowHost,
    private val scope: CoroutineScope,
    private var publish: (BackupUiState.Step) -> Unit,
    private var finish: (BackupDocumentCopy?, BackupUiState.Notice) -> Unit,
) : Closeable {
    private var request: BackupDeletionRequest? = null
    private var step = BackupUiState.Step.IDLE
    private var job: Job? = null
    private var closed = false

    fun start() {
        if (closed || step != BackupUiState.Step.IDLE) return
        request = try { host.openBackupDeletionRequest() } catch (_: RuntimeException) { null }
        if (request == null) complete(null, BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE)
        else transition(BackupUiState.Step.PICK_DELETE)
    }

    fun claimRequest(): BackupDeletionRequest? {
        if (closed || step != BackupUiState.Step.PICK_DELETE) return null
        val owned = request ?: return null
        transition(BackupUiState.Step.WAITING_DELETE)
        return owned
    }

    fun onResult(owned: BackupDeletionRequest, document: Uri?) {
        if (closed || request !== owned || step != BackupUiState.Step.WAITING_DELETE) return
        if (document == null) {
            complete(null, BackupUiState.Notice.DELETE_SELECTION_CANCELLED)
            return
        }
        transition(BackupUiState.Step.PREPARING_DELETE)
        job = scope.launch(start = CoroutineStart.LAZY) {
            var copy: BackupDocumentCopy? = null
            try {
                copy = host.completeBackupDeletionSelection(owned, document)
                currentCoroutineContext().ensureActive()
                // A provider label is untrusted metadata, never an archive-authentication result.
                if (closed || copy?.displayName?.let(::isBackupDocumentNameSafe) != true) {
                    copy?.close()
                    copy = null
                }
                if (!closed) {
                    val accepted = copy
                    copy = null
                    complete(accepted, if (accepted == null) BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE
                        else BackupUiState.Notice.NONE)
                }
            } catch (_: CancellationException) {
                currentCoroutineContext().ensureActive()
                if (!closed) complete(null, BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE)
            } catch (_: RuntimeException) {
                if (!closed) complete(null, BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE)
            } finally {
                copy?.close()
                owned.close()
            }
        }
        job?.start()
    }

    fun onPickerUnavailable(owned: BackupDeletionRequest) {
        if (!closed && request === owned && step == BackupUiState.Step.WAITING_DELETE) {
            complete(null, BackupUiState.Notice.DELETE_SELECTION_UNAVAILABLE)
        }
    }

    private fun transition(next: BackupUiState.Step) {
        step = next
        publish(next)
    }

    private fun complete(copy: BackupDocumentCopy?, notice: BackupUiState.Notice) {
        val completed = finish
        close()
        completed(copy, notice)
    }

    override fun close() {
        if (closed) return
        closed = true
        publish = {}
        finish = { copy, _ -> copy?.close() }
        request?.close()
        request = null
        job?.cancel()
        job = null
    }
}
