// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState

/**
 * Turns bounded profile snapshots into an equally bounded stream of notification changes.
 *
 * Historical terminal rows are baseline state, not new events, so process restoration never
 * announces an old download again. Active rows are restored immediately. A terminal row is shown
 * only when it follows an active row or first appears after the initial snapshot settles.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class TaffyDownloadNotificationProjection {
    sealed interface Effect {
        data class Show(val record: DownloadRecord) : Effect

        data class Cancel(val id: DownloadId) : Effect
    }

    private val tracked = linkedMapOf<String, DownloadRecord>()
    private var snapshotSettled = false

    fun accept(records: List<DownloadRecord>): List<Effect> {
        val bounded = records.take(MAX_TRACKED_DOWNLOADS)
        val nextIds = bounded.asSequence().map { it.id.value }.toHashSet()
        val effects = mutableListOf<Effect>()

        for ((token, previous) in tracked) {
            if (token !in nextIds) effects += Effect.Cancel(previous.id)
        }
        for (record in bounded) {
            val previous = tracked[record.id.value]
            if (previous == record) continue
            if (shouldShow(record, previous)) effects += Effect.Show(record)
        }

        tracked.clear()
        bounded.forEach { tracked[it.id.value] = it }
        return effects
    }

    /** Marks a provider snapshot as pending so unseen terminal history stays silent. */
    fun beginSnapshot() {
        snapshotSettled = false
    }

    /** Allows later live terminal rows to be announced after a successful provider snapshot. */
    fun settleSnapshot() {
        snapshotSettled = true
    }

    /** Forgets a failed platform post so a later provider tick can retry it. */
    fun retryOnNextSnapshot(id: DownloadId) {
        tracked.remove(id.value)
    }

    fun close(): List<Effect.Cancel> {
        val effects = tracked.values.map { Effect.Cancel(it.id) }
        tracked.clear()
        return effects
    }

    private fun shouldShow(record: DownloadRecord, previous: DownloadRecord?): Boolean {
        if (record.state == DownloadState.RUNNING || record.state == DownloadState.PAUSED) {
            return true
        }
        if (snapshotSettled) return true
        return previous?.state == DownloadState.RUNNING || previous?.state == DownloadState.PAUSED
    }

    private companion object {
        const val MAX_TRACKED_DOWNLOADS = 256
    }
}
