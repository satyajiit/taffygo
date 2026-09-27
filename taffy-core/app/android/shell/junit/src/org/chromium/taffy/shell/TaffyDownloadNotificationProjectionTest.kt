// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.shell.TaffyDownloadNotificationProjection.Effect
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadNotificationProjectionTest {
    @Test
    fun initialSnapshotRestoresActiveWorkButDoesNotReannounceTerminalHistory() {
        val projection = TaffyDownloadNotificationProjection()
        projection.beginSnapshot()

        val effects = projection.accept(
            listOf(record("old", DownloadState.COMPLETE), record("live", DownloadState.RUNNING)),
        )

        assertEquals(listOf(Effect.Show(record("live", DownloadState.RUNNING))), effects)
    }

    @Test
    fun equalSnapshotsDoNotDuplicateAndTerminalTransitionIsShownOnce() {
        val projection = TaffyDownloadNotificationProjection()
        val running = record("one", DownloadState.RUNNING)
        projection.beginSnapshot()
        assertEquals(listOf(Effect.Show(running)), projection.accept(listOf(running)))
        projection.settleSnapshot()
        assertTrue(projection.accept(listOf(running)).isEmpty())

        val complete = record("one", DownloadState.COMPLETE)
        assertEquals(listOf(Effect.Show(complete)), projection.accept(listOf(complete)))
        assertTrue(projection.accept(listOf(complete)).isEmpty())
    }

    @Test
    fun rebuiltSnapshotDoesNotTurnAnOlderRefillIntoANewCompletion() {
        val projection = TaffyDownloadNotificationProjection()
        val running = record("live", DownloadState.RUNNING)
        projection.beginSnapshot()
        projection.accept(listOf(running))
        projection.settleSnapshot()

        projection.beginSnapshot()
        val effects = projection.accept(
            listOf(running, record("older", DownloadState.COMPLETE)),
        )

        assertTrue(effects.isEmpty())
    }

    @Test
    fun removalAndProfileCloseCancelOnlyTrackedOpaqueIdentities() {
        val projection = TaffyDownloadNotificationProjection()
        val first = record("first", DownloadState.RUNNING)
        val second = record("second", DownloadState.PAUSED)
        projection.accept(listOf(first, second))

        assertEquals(listOf(Effect.Cancel(first.id)), projection.accept(listOf(second)))
        assertEquals(listOf(Effect.Cancel(second.id)), projection.close())
    }

    @Test
    fun failedPlatformPostCanRetryButProjectionNeverGrowsPastContractBound() {
        val projection = TaffyDownloadNotificationProjection()
        val records = (0 until 300).map { record("item-$it", DownloadState.RUNNING) }
        val effects = projection.accept(records)
        assertEquals(256, effects.size)

        val first = records.first()
        projection.retryOnNextSnapshot(first.id)
        assertEquals(listOf(Effect.Show(first)), projection.accept(records).take(1))
        assertEquals(256, projection.close().size)
    }

    private fun record(id: String, state: DownloadState) = DownloadRecord(
        id = DownloadId(id),
        fileName = "$id.bin",
        host = "example.test",
        totalBytes = 10,
        downloadedBytes = if (state == DownloadState.COMPLETE) 10 else 4,
        state = state,
        allowedActions = when (state) {
            DownloadState.RUNNING -> setOf(DownloadAction.PAUSE, DownloadAction.CANCEL)
            DownloadState.PAUSED -> setOf(DownloadAction.RESUME, DownloadAction.CANCEL)
            DownloadState.COMPLETE -> setOf(DownloadAction.OPEN, DownloadAction.SHARE)
            DownloadState.FAILED -> setOf(DownloadAction.RESUME)
        },
    )
}
