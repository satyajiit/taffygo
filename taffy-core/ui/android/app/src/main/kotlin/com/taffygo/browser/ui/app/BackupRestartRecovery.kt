// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.annotation.MainThread
import java.io.Closeable
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.isActive

/** Owns pending discovery and its fresh review. No key, import, stage or commit is reconstructed. */
@MainThread
internal class BackupRestartRecovery(
    private val host: BackupWindowHost,
    private val scope: CoroutineScope,
    private var publish: (BackupRestoreUiState) -> Unit,
    private var finish: (BackupUiState.Notice) -> Unit,
) : Closeable {
    private var request: BackupRestoreDiscoveryRequest? = null
    private var ceremony: BackupRestoreCeremony? = null
    private var deliveredReview: BackupRestoreReview? = null
    private var started = false
    private var claimed = false
    private var closed = false

    fun start() {
        if (closed || started) return
        if (!scope.isActive) {
            close()
            return
        }
        started = true
        publish(BackupRestoreUiState(step = BackupRestoreUiState.Step.DISCOVERING, recovered = true))
        if (closed || !scope.isActive) {
            close()
            return
        }
        val opened = try {
            host.discoverInterruptedRestore(::onDiscovery)
        } catch (_: RuntimeException) {
            null
        }
        if (closed || claimed) opened?.close()
        else if (opened == null) onDiscovery(BackupRestoreDiscoveryResult.Unavailable)
        else request = opened
    }

    fun onIntent(intent: BackupIntent, resumed: Boolean) {
        if (!closed) ceremony?.onIntent(intent, resumed)
    }

    private fun onDiscovery(result: BackupRestoreDiscoveryResult) {
        // The host promises UI-thread delivery. Do not post Ready into a cancelled coroutine scope:
        // a body which never runs cannot relinquish the native token it was meant to adopt.
        val ready = result as? BackupRestoreDiscoveryResult.Ready
        if (closed || claimed || !scope.isActive) {
            if (ready != null && (closed || ready.review !== deliveredReview)) {
                host.abandonImportedRestoreReview(ready.review)
            }
            if (!scope.isActive) close()
            return
        }
        claimed = true
        request?.close()
        request = null
        when (result) {
            is BackupRestoreDiscoveryResult.Ready -> {
                deliveredReview = result.review
                val next = BackupRestoreCeremony(host, scope, publish, ::end)
                ceremony = next
                next.startRecovered(result.review, result.cleanupOnly)
            }
            is BackupRestoreDiscoveryResult.RecoveryRequired -> publish(BackupRestoreUiState(
                step = BackupRestoreUiState.Step.NEEDS_RECOVERY, recovered = true, recoveryReason = result.reason,
            ))
            BackupRestoreDiscoveryResult.None -> end(BackupUiState.Notice.RECOVERY_NONE)
            BackupRestoreDiscoveryResult.SourceUnavailable -> end(BackupUiState.Notice.RECOVERY_SOURCE_UNAVAILABLE)
            BackupRestoreDiscoveryResult.Unavailable -> end(BackupUiState.Notice.RECOVERY_UNAVAILABLE)
            BackupRestoreDiscoveryResult.AlreadyKept -> end(BackupUiState.Notice.RECOVERY_ALREADY_KEPT)
            BackupRestoreDiscoveryResult.AlreadyDiscarded -> end(BackupUiState.Notice.RECOVERY_ALREADY_DISCARDED)
        }
    }

    private fun end(notice: BackupUiState.Notice) {
        if (closed) return
        val completed = finish
        close()
        completed(notice)
    }

    override fun close() {
        if (closed) return
        closed = true
        publish = {}
        finish = {}
        request?.close()
        request = null
        ceremony?.close()
        ceremony = null
        deliveredReview = null
    }
}
