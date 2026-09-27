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
import android.graphics.drawable.Icon
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import org.chromium.taffy.shell.TaffyDownloadNotification.Action
import org.chromium.taffy.shell.TaffyDownloadNotification.State

/** Android posting adapter for SCR-802; profile observation and decisions stay outside it. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class TaffyDownloadNotifier(
    context: Context,
    private val manager: NotificationManager? =
        context.applicationContext.getSystemService(NotificationManager::class.java),
) : TaffyDownloadNotificationSink {
    private val context = context.applicationContext

    override fun show(profileToken: String, record: DownloadRecord): Boolean {
        val notificationManager = manager ?: return false
        // Channel creation is idempotent. Retry it with each provider tick instead of latching a
        // transient framework failure for the lifetime of the profile notification controller.
        if (!ensureChannel() || !notificationsAvailable(notificationManager)) return false
        return try {
            notificationManager.notify(
                TaffyDownloadActionPayload.notificationTag(profileToken, record.id),
                NOTIFICATION_ID,
                build(profileToken, record),
            )
            true
        } catch (_: RuntimeException) {
            false
        }
    }

    override fun cancel(profileToken: String, id: DownloadId) {
        try {
            manager?.cancel(
                TaffyDownloadActionPayload.notificationTag(profileToken, id),
                NOTIFICATION_ID,
            )
        } catch (_: RuntimeException) {
            // Android may withdraw the notification service while a profile is closing.
        }
    }

    override fun reconcile(profileToken: String, retainedIds: Set<DownloadId>) {
        val retainedTags = retainedIds.mapTo(hashSetOf()) { id ->
            TaffyDownloadActionPayload.notificationTag(profileToken, id)
        }
        cancelMatching(profileToken) { tag -> tag !in retainedTags }
    }

    override fun cancelProfile(profileToken: String) {
        cancelMatching(profileToken) { true }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    fun build(profileToken: String, record: DownloadRecord): Notification {
        val state = stateFor(record.state)
        val actions = TaffyDownloadNotification.actionsFor(
            state,
            DownloadAction.PAUSE in record.allowedActions,
            DownloadAction.RESUME in record.allowedActions,
            DownloadAction.CANCEL in record.allowedActions,
        )
        val builder = Notification.Builder(context, TaffyDownloadNotification.CHANNEL_ID)
            .setContentTitle(
                context.getString(TaffyDownloadNotification.titleFor(state), record.fileName),
            )
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setOngoing(!TaffyDownloadNotification.isDismissable(state))
            .setAutoCancel(TaffyDownloadNotification.isDismissable(state))
            .setOnlyAlertOnce(true)
            .setLocalOnly(true)
            .setVisibility(Notification.VISIBILITY_PRIVATE)
            .setPublicVersion(publicVersion())

        if (TaffyDownloadNotification.showsProgress(state)) {
            val percent = record.fraction?.let { (it * 100).toInt().coerceIn(0, 100) }
            builder.setProgress(100, percent ?: 0, percent == null)
        }
        for (action in actions) {
            builder.addAction(
                Notification.Action.Builder(
                    Icon.createWithResource(context, android.R.drawable.stat_sys_download),
                    context.getString(TaffyDownloadNotification.labelFor(action)),
                    TaffyDownloadActionPayload.pendingIntent(
                        context,
                        profileToken,
                        record.id,
                        action,
                    ),
                ).build(),
            )
        }
        return builder.build()
    }

    private fun ensureChannel(): Boolean {
        val notificationManager = manager ?: return false
        return try {
            val channel = NotificationChannel(
                TaffyDownloadNotification.CHANNEL_ID,
                context.getString(R.string.taffy_download_channel_name),
                NotificationManager.IMPORTANCE_LOW,
            )
            channel.description = context.getString(R.string.taffy_download_channel_description)
            notificationManager.createNotificationChannel(channel)
            true
        } catch (_: RuntimeException) {
            false
        }
    }

    private fun notificationsAvailable(notificationManager: NotificationManager): Boolean = try {
        notificationManager.areNotificationsEnabled() &&
            notificationManager
                .getNotificationChannel(TaffyDownloadNotification.CHANNEL_ID)
                ?.importance
                ?.let { importance -> importance != NotificationManager.IMPORTANCE_NONE } == true
    } catch (_: RuntimeException) {
        false
    }

    private fun publicVersion(): Notification =
        Notification.Builder(context, TaffyDownloadNotification.CHANNEL_ID)
            .setContentTitle(context.getString(R.string.taffy_download_hidden))
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .build()

    private fun cancelMatching(profileToken: String, shouldCancel: (String) -> Boolean) {
        val notificationManager = manager ?: return
        val prefix = "${TaffyDownloadActionPayload.notificationProfilePrefix(profileToken)}:"
        val active = try {
            notificationManager.activeNotifications
        } catch (_: RuntimeException) {
            return
        }
        for (entry in active) {
            val tag = entry.tag ?: continue
            if (entry.id == NOTIFICATION_ID && tag.startsWith(prefix) && shouldCancel(tag)) {
                try {
                    notificationManager.cancel(tag, NOTIFICATION_ID)
                } catch (_: RuntimeException) {
                    return
                }
            }
        }
    }

    private fun stateFor(state: DownloadState): State = when (state) {
        DownloadState.RUNNING -> State.RUNNING
        DownloadState.PAUSED -> State.PAUSED
        DownloadState.COMPLETE -> State.COMPLETE
        DownloadState.FAILED -> State.FAILED
    }

    private companion object {
        const val NOTIFICATION_ID = 802
    }
}
