// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import org.chromium.base.lifetime.Destroyable

/** One bounded cache publication per UI turn, even when many reads finish together. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class CoalescedSiteMarkPublisher<T : Any>(
    private val snapshot: () -> Map<String, T>,
    private val schedule: (Runnable) -> Unit,
    private val cancel: (Runnable) -> Unit,
) : Destroyable {
    private var pending: Runnable? = null
    private var callback: ((Map<String, T>) -> Unit)? = null
    private var destroyed = false

    fun request(publish: (Map<String, T>) -> Unit) {
        check(!destroyed) { "A destroyed site-mark publisher cannot act" }
        callback = publish
        if (pending != null) return
        val task = Runnable {
            pending = null
            val publishLatest = callback
            callback = null
            if (!destroyed) publishLatest?.invoke(snapshot())
        }
        pending = task
        schedule(task)
    }

    override fun destroy() {
        if (destroyed) return
        destroyed = true
        pending?.let(cancel)
        pending = null
        callback = null
    }
}
