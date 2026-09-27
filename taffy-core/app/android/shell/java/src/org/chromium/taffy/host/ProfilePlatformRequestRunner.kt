// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.annotation.VisibleForTesting
import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import org.chromium.taffy.core_service.mojom.EffectStatus

/** Owns the cancellable work started by one profile platform Mojo binding. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class ProfilePlatformRequestRunner(
    parentScope: CoroutineScope,
) : Closeable {
    private val closed = AtomicBoolean(false)
    private val requestJob = SupervisorJob(parentScope.coroutineContext[Job])
    private val requestScope = CoroutineScope(parentScope.coroutineContext + requestJob)

    fun launch(
        block: suspend () -> Unit,
        failure: (Int) -> Unit,
    ) {
        if (closed.get()) {
            failure(EffectStatus.UNAVAILABLE)
            return
        }
        requestScope.launch {
            try {
                block()
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: IllegalArgumentException) {
                failure(EffectStatus.INVALID_RESULT)
            } catch (_: IllegalStateException) {
                failure(EffectStatus.UNAVAILABLE)
            } catch (_: RuntimeException) {
                failure(EffectStatus.UNAVAILABLE)
            }
        }
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        requestScope.cancel()
    }
}
