// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.any
import org.mockito.ArgumentMatchers.anyInt
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.mock
import org.mockito.Mockito.never
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.robolectric.Shadows.shadowOf

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadNotifierTest {
    private val context: Context = ApplicationProvider.getApplicationContext()
    private val manager = context.getSystemService(NotificationManager::class.java)
    private val notifier = TaffyDownloadNotifier(context, manager)
    private val profileToken = "n".repeat(43)

    @After
    fun clearNotifications() {
        manager.cancelAll()
    }

    @Test
    fun runningNotificationIsPrivateLocalOnlyOngoingAndExactlyControllable() {
        val record = record(
            id = "one",
            state = DownloadState.RUNNING,
            actions = setOf(DownloadAction.PAUSE, DownloadAction.CANCEL),
        )

        val notification = notifier.build(profileToken, record)

        assertEquals(Notification.VISIBILITY_PRIVATE, notification.visibility)
        assertTrue(notification.flags and Notification.FLAG_LOCAL_ONLY != 0)
        assertTrue(notification.flags and Notification.FLAG_ONGOING_EVENT != 0)
        assertFalse(notification.flags and Notification.FLAG_AUTO_CANCEL != 0)
        assertEquals(40, notification.extras.getInt(Notification.EXTRA_PROGRESS))
        assertFalse(notification.extras.getBoolean(Notification.EXTRA_PROGRESS_INDETERMINATE))
        val notificationActions = notification.actions.orEmpty()
        assertEquals(
            listOf(
                context.getString(R.string.taffy_download_pause),
                context.getString(R.string.taffy_download_cancel),
            ),
            notificationActions.map { it.title.toString() },
        )
        assertEquals(
            listOf(DownloadAction.PAUSE, DownloadAction.CANCEL),
            notificationActions.map { action ->
                TaffyDownloadActionPayload.parse(shadowOf(action.actionIntent).savedIntent)?.action
            },
        )
        val publicVersion = requireNotNull(notification.publicVersion)
        assertNotNull(publicVersion)
        assertEquals(
            context.getString(R.string.taffy_download_hidden),
            publicVersion.extras.getString(Notification.EXTRA_TITLE),
        )
    }

    @Test
    fun terminalNotificationIsDismissibleAndDoesNotCarryOpenOrShare() {
        val notification = notifier.build(
            profileToken,
            record(
                id = "done",
                state = DownloadState.COMPLETE,
                actions = setOf(DownloadAction.OPEN, DownloadAction.SHARE, DownloadAction.REMOVE),
            ),
        )

        assertFalse(notification.flags and Notification.FLAG_ONGOING_EVENT != 0)
        assertTrue(notification.flags and Notification.FLAG_AUTO_CANCEL != 0)
        assertTrue(notification.actions.orEmpty().isEmpty())
    }

    @Test
    fun completeSnapshotReconciliationAndProfileCloseRemoveOnlyOwnedNotifications() {
        val first = record("first", DownloadState.RUNNING, setOf(DownloadAction.CANCEL))
        val second = record("second", DownloadState.PAUSED, setOf(DownloadAction.RESUME))
        assertTrue(notifier.show(profileToken, first))
        assertTrue(notifier.show(profileToken, second))
        assertEquals(2, manager.activeNotifications.size)

        notifier.reconcile(profileToken, setOf(first.id))
        assertEquals(
            setOf(TaffyDownloadActionPayload.notificationTag(profileToken, first.id)),
            manager.activeNotifications.mapNotNull { it.tag }.toSet(),
        )

        notifier.cancelProfile(profileToken)
        assertTrue(manager.activeNotifications.isEmpty())
    }

    @Test
    fun disabledChannelIsAReportedPostFailureAndNothingIsPublished() {
        val disabledManager = mock(NotificationManager::class.java)
        `when`(disabledManager.areNotificationsEnabled()).thenReturn(true)
        `when`(
            disabledManager.getNotificationChannel(TaffyDownloadNotification.CHANNEL_ID),
        ).thenReturn(
            NotificationChannel(
                TaffyDownloadNotification.CHANNEL_ID,
                "Downloads",
                NotificationManager.IMPORTANCE_NONE,
            ),
        )
        val disabledNotifier = TaffyDownloadNotifier(context, disabledManager)

        assertFalse(
            disabledNotifier.show(
                profileToken,
                record("blocked", DownloadState.RUNNING, setOf(DownloadAction.CANCEL)),
            ),
        )
        verify(disabledManager, never()).notify(anyString(), anyInt(), any())
    }

    private fun record(
        id: String,
        state: DownloadState,
        actions: Set<DownloadAction>,
    ) = DownloadRecord(
        id = DownloadId("download:$id"),
        fileName = "$id.bin",
        host = "example.test",
        totalBytes = 10,
        downloadedBytes = 4,
        state = state,
        allowedActions = actions,
    )
}
