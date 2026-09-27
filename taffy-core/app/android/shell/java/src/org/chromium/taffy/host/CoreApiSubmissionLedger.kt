// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException

/** UI-sequence ledger that resolves every submitted Core API command at most once. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class CoreApiSubmissionLedger {
    private val lock = Any()
    private var nextId = 1L
    private val pending = linkedMapOf<Long, Terminal>()

    fun register(terminal: Terminal): Long = synchronized(lock) {
        check(nextId > 0L) { "Core API submission identifiers are exhausted" }
        nextId++.also { id -> check(pending.put(id, terminal) == null) }
    }

    fun complete(
        id: Long,
        failure: CoreApiSubmissionException.Reason? = null,
    ): Boolean {
        val terminal = synchronized(lock) { pending.remove(id) } ?: return false
        terminal(failure)
        return true
    }

    fun cancel(id: Long): Boolean = synchronized(lock) { pending.remove(id) != null }

    fun failAll(failure: CoreApiSubmissionException.Reason) {
        val terminals = synchronized(lock) {
            pending.values.toList().also { pending.clear() }
        }
        terminals.forEach { terminal -> terminal(failure) }
    }

    fun sizeForTesting(): Int = synchronized(lock) { pending.size }
}

typealias Terminal = (CoreApiSubmissionException.Reason?) -> Unit
