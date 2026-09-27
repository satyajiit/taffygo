// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.di

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureScope
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.internal.CoroutineFailureHandlers
import java.io.Closeable
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel

/** A close-once coroutine and resource lifetime owned by a Chromium object. */
abstract class TaffyLifetime(
    dispatchers: AppDispatchers,
    failureSink: CoroutineFailureSink,
    failureScope: CoroutineFailureScope,
) : Closeable {
    private val lock = Any()
    private var closed = false
    private val closeables = mutableListOf<Closeable>()

    /** Work that is cancelled before registered resources are closed. */
    val scope: CoroutineScope = CoroutineScope(
        SupervisorJob() +
            dispatchers.default +
            CoroutineFailureHandlers.create(failureScope, failureSink),
    )

    /** Register a resource that must not outlive this Chromium owner. */
    fun own(closeable: Closeable) {
        synchronized(lock) {
            check(!closed) { "Cannot attach a resource to a closed Taffy lifetime" }
            closeables += closeable
        }
    }

    /** Cancel and close exactly once. Further calls are no-ops. */
    final override fun close() {
        val owned = synchronized(lock) {
            if (closed) return
            closed = true
            closeables.asReversed().toList().also { closeables.clear() }
        }
        scope.cancel()
        var failure: Throwable? = null
        for (resource in owned) {
            try {
                resource.close()
            } catch (next: Throwable) {
                failure?.addSuppressed(next) ?: run { failure = next }
            }
        }
        failure?.let { throw it }
    }
}
