// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import org.chromium.taffy.browser.TaffyBackupWorkflowBridge

/** Opaque key ceremony and withdrawal handle retained for the whole workflow. */
class ChromiumBackupSession(
    private val state: ChromiumBackupWindowState,
    val operationId: String,
    override val mode: BackupRecoveryKeySession.Mode,
) : BackupRecoveryKeySession {
    var phase = BackupSessionPhase.KEY
    var archiveBytes = 0L
    var maximumBytes = 0L
    var restoreCapability: Any? = null
    var physicalCustodyTransferred = false
    private var nativeWithdrawalInFlight = false
    private var nativeWithdrawalComplete = false
    private var nativeWithdrawalRetryRequested = false

    override fun takeGeneratedKeyForDisplay(): CharArray? = synchronized(state.lock) {
        if (state.closed || !state.active || this !in state.sessions ||
            mode != BackupRecoveryKeySession.Mode.CREATE || phase != BackupSessionPhase.KEY
        ) null else state.native?.takeGeneratedKeyForDisplay(operationId)
    }

    override fun confirmKeyRetained(): BackupRecoveryKeySession.Acceptance {
        val result = synchronized(state.lock) {
            if (state.closed || !state.active || this !in state.sessions ||
                mode != BackupRecoveryKeySession.Mode.CREATE || phase != BackupSessionPhase.KEY
            ) TaffyBackupWorkflowBridge.UNAVAILABLE else {
                state.native?.confirmKeyRetained(operationId)
                    ?: TaffyBackupWorkflowBridge.UNAVAILABLE
            }
        }
        if (result == TaffyBackupWorkflowBridge.ACCEPTED) {
            synchronized(state.lock) {
                if (this in state.sessions) phase = BackupSessionPhase.CREATE_CONFIRMED
            }
        }
        return result.toBackupAcceptance()
    }

    override fun acceptEnteredKey(text: CharArray): BackupRecoveryKeySession.Acceptance {
        val result = synchronized(state.lock) {
            if (state.closed || !state.active || this !in state.sessions ||
                mode != BackupRecoveryKeySession.Mode.RESTORE || phase != BackupSessionPhase.KEY
            ) TaffyBackupWorkflowBridge.UNAVAILABLE else {
                state.native?.acceptEnteredKey(operationId, text)
                    ?: TaffyBackupWorkflowBridge.UNAVAILABLE
            }
        }
        if (result != TaffyBackupWorkflowBridge.ACCEPTED) return result.toBackupAcceptance()
        val maximum = state.native?.maximumImportBytes(operationId) ?: -1
        val usable = synchronized(state.lock) {
            if (state.closed || this !in state.sessions || maximum <= 0) {
                state.finishLocked(this)
                false
            } else {
                maximumBytes = maximum
                phase = BackupSessionPhase.IMPORT_READY
                true
            }
        }
        if (!usable) {
            withdrawNative()
            return BackupRecoveryKeySession.Acceptance.UNAVAILABLE
        }
        return BackupRecoveryKeySession.Acceptance.ACCEPTED
    }

    override fun cancel() {
        synchronized(state.lock) {
            if (this in state.sessions) state.finishLocked(this)
        }
        try {
            withdrawNative()
        } catch (_: RuntimeException) {
            // Cancellation is a nonthrowing UI seam. Pre-commit withdrawal
            // stays retryable; post-dispatch custody is never reissued.
        }
    }

    fun withdrawNative() {
        val owner = synchronized(state.lock) {
            if (nativeWithdrawalComplete || nativeWithdrawalInFlight) {
                if (nativeWithdrawalInFlight && !physicalCustodyTransferred) {
                    nativeWithdrawalRetryRequested = true
                }
                return
            }
            nativeWithdrawalInFlight = true
            if (physicalCustodyTransferred) nativeWithdrawalComplete = true
            state.native
        }
        var complete = owner == null
        try {
            owner?.abandon(operationId)
            complete = true
        } finally {
            val retry = synchronized(state.lock) {
                nativeWithdrawalInFlight = false
                if (complete) {
                    nativeWithdrawalComplete = true
                    nativeWithdrawalRetryRequested = false
                    false
                } else if (!nativeWithdrawalComplete && nativeWithdrawalRetryRequested) {
                    nativeWithdrawalRetryRequested = false
                    true
                } else {
                    false
                }
            }
            if (retry) withdrawNative()
        }
    }

    fun handoffToOwnerCloseLocked() {
        nativeWithdrawalComplete = true
        nativeWithdrawalRetryRequested = false
    }
}

private fun Int.toBackupAcceptance() = when (this) {
    TaffyBackupWorkflowBridge.ACCEPTED -> BackupRecoveryKeySession.Acceptance.ACCEPTED
    TaffyBackupWorkflowBridge.REFUSED -> BackupRecoveryKeySession.Acceptance.REFUSED
    else -> BackupRecoveryKeySession.Acceptance.UNAVAILABLE
}
