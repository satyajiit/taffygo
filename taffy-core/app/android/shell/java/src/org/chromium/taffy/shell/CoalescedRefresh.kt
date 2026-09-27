// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import java.io.Closeable

/** Unions a synchronous model-callback burst into one next-loop projection refresh. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class CoalescedRefresh(
    private val schedule: (Runnable) -> Boolean,
    private val cancel: (Runnable) -> Unit,
    private val refresh: () -> Unit,
) : Closeable {
    private var scheduled = false
    private var closed = false
    private val task = Runnable {
        if (closed || !scheduled) return@Runnable
        scheduled = false
        refresh()
    }

    fun request() {
        if (closed || scheduled) return
        scheduled = true
        if (!schedule(task)) {
            scheduled = false
            refresh()
        }
    }

    override fun close() {
        if (closed) return
        closed = true
        if (scheduled) cancel(task)
        scheduled = false
    }
}
