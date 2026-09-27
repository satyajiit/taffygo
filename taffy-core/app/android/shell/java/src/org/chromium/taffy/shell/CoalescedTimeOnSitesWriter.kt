// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import java.io.Closeable

/** Coalesces foreground navigation bursts into one bounded ledger write. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class CoalescedTimeOnSitesWriter(
    private val schedule: (Runnable, Long) -> Boolean,
    private val cancel: (Runnable) -> Unit,
    private val write: () -> Unit,
) : Closeable {
    private var pending = false
    private var closed = false
    private val writeTask = Runnable {
        if (closed || !pending) return@Runnable
        pending = false
        write()
    }

    fun request() {
        if (closed || pending) return
        pending = true
        if (!schedule(writeTask, WRITE_DELAY_MILLIS)) {
            pending = false
            write()
        }
    }

    /** Writes synchronously at lifecycle, checkpoint, and privacy boundaries. */
    fun flush() {
        if (closed || !pending) return
        cancel(writeTask)
        pending = false
        write()
    }

    override fun close() {
        if (closed) return
        flush()
        closed = true
    }

    private companion object {
        const val WRITE_DELAY_MILLIS = 250L
    }
}
