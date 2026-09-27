// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.feature.settings.ProfileDataControl
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/** Orders authority withdrawal before Android's asynchronous all-application-data primitive. */
class ProfileDataErasureCoordinator(
    private val withdrawWindowAndTabAuthority: () -> Unit,
    private val cancelTask: suspend (String) -> Unit,
    private val closeRemainingProfileAuthority: () -> Unit,
    private val destroyCredentialMaterial: suspend () -> Unit,
    private val requestApplicationDataClear: () -> Boolean,
) {
    private val lock = Mutex()
    private var attempted = false

    suspend fun erase(activeTasks: ActiveTasks): ProfileDataControl.DeletionResult =
        withContext(NonCancellable) {
            lock.withLock {
                if (attempted) return@withLock ProfileDataControl.DeletionResult.UNAVAILABLE
                attempted = true

                // Cleanup failures are recorded by the rejected-request result, but no one failure
                // may prevent the stronger OS erase from being attempted.
                attempt(withdrawWindowAndTabAuthority)
                if (activeTasks is ActiveTasks.Known) {
                    activeTasks.ids.distinct().sorted().forEach { taskId ->
                        attemptSuspend { cancelTask(taskId) }
                    }
                }
                attempt(closeRemainingProfileAuthority)
                attemptSuspend(destroyCredentialMaterial)

                val accepted = try {
                    requestApplicationDataClear()
                } catch (_: RuntimeException) {
                    false
                }
                if (accepted) {
                    // Android owns the subsequent kill. Acceptance is a started outcome, never a
                    // claim that the now-doomed process observed completed deletion.
                    ProfileDataControl.DeletionResult.STARTED
                } else {
                    ProfileDataControl.DeletionResult.FAILED
                }
            }
        }

    /** Whether the complete pre-erasure task list was available. */
    sealed interface ActiveTasks {
        data class Known(val ids: List<String>) : ActiveTasks
        data object Unknown : ActiveTasks
    }

    private inline fun attempt(block: () -> Unit) {
        try {
        block()
        } catch (_: Throwable) {
            // The OS clear below remains the fail-closed fallback.
        }
    }

    private suspend inline fun attemptSuspend(crossinline block: suspend () -> Unit) {
        try {
            block()
        } catch (_: Throwable) {
            // The OS clear below remains the fail-closed fallback.
        }
    }
}
