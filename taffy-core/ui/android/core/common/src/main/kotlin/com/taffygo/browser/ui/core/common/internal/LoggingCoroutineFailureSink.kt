// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.internal

import com.taffygo.browser.ui.core.common.CoroutineFailureScope
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.LogLevel
import com.taffygo.browser.ui.core.common.Logger
import java.util.concurrent.atomic.AtomicLongArray

/** Records a content-free counter before emitting a fixed diagnostic. */
internal class LoggingCoroutineFailureSink(
    private val logger: Logger,
) : CoroutineFailureSink {
    private val counts = AtomicLongArray(CoroutineFailureScope.entries.size)

    override fun record(scope: CoroutineFailureScope) {
        counts.incrementAndGet(scope.ordinal)
        try {
            logger.log(LogLevel.ERROR, LOG_TAG, scope.fixedMessage())
        } catch (_: RuntimeException) {
            // The counter already recorded the failure. Diagnostics cannot
            // become a second uncaught root failure.
        }
    }

    private fun CoroutineFailureScope.fixedMessage(): String = when (this) {
        CoroutineFailureScope.PROCESS -> "Uncaught process coroutine failure"
        CoroutineFailureScope.PROFILE -> "Uncaught profile coroutine failure"
        CoroutineFailureScope.WINDOW -> "Uncaught window coroutine failure"
        CoroutineFailureScope.TAB -> "Uncaught tab coroutine failure"
    }

    private companion object {
        const val LOG_TAG = "TaffyCoroutine"
    }
}
