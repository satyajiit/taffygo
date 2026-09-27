// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.drawable.Icon
import android.os.Build
import androidx.core.content.ContextCompat
import com.taffygo.browser.ui.core.common.di.TaffyApplicationContext

/** Android poster for SCR-801; task authority never enters this class. */
internal class AndroidTaskContinuationPlatform(
    @TaffyApplicationContext context: Context,
) : TaskContinuationPlatform {
    private val context = context.applicationContext
    private val manager = requireNotNull(
        this.context.getSystemService(NotificationManager::class.java),
    ) { "Android notification service is unavailable" }
    private val factory = TaskNotificationFactory(this.context)

    init {
        ensureChannel()
    }

    override fun canPostNotifications(): Boolean {
        if (!manager.areNotificationsEnabled()) return false
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            return false
        }
        return manager.getNotificationChannel(CHANNEL_ID)?.importance !=
            NotificationManager.IMPORTANCE_NONE
    }

    override fun publish(
        projection: TaskNotificationProjection,
        requireForeground: Boolean,
    ): Boolean {
        val notification = factory.build(projection)
        return try {
            manager.notify(projection.notificationId, notification)
            when {
                requireForeground &&
                    TaskContinuationService.promotePrepared(projection) -> Unit
                requireForeground -> ContextCompat.startForegroundService(
                    context,
                    TaskContinuationService.activationIntent(context, projection),
                )
                projection.requiresForeground &&
                    TaskContinuationService.prepareLive(projection) -> Unit
                projection.requiresForeground -> requireNotNull(
                    context.startService(
                        TaskContinuationService.preparationIntent(context, projection),
                    ),
                ) { "Task continuation service could not be prepared" }
                else -> TaskContinuationService.releaseForeground(
                    projection.profileToken,
                    removeNotification = false,
                )
            }
            true
        } catch (_: RuntimeException) {
            manager.cancel(projection.notificationId)
            TaskContinuationService.releaseForeground(
                projection.profileToken,
                removeNotification = true,
            )
            false
        }
    }

    override fun remove(profileToken: String, notificationId: Int) {
        TaskContinuationService.releaseForeground(
            profileToken,
            removeNotification = true,
        )
        manager.cancel(notificationId)
    }

    private fun ensureChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            context.getString(R.string.taffy_task_notification_channel_name),
            NotificationManager.IMPORTANCE_LOW,
        ).apply {
            description = context.getString(R.string.taffy_task_notification_channel_description)
            lockscreenVisibility = Notification.VISIBILITY_PRIVATE
            setShowBadge(false)
        }
        manager.createNotificationChannel(channel)
    }

    companion object {
        const val CHANNEL_ID = "taffygo.tasks"
    }
}

/** Builds only compiled, content-free notification text and immutable intents. */
internal class TaskNotificationFactory(private val context: Context) {
    fun build(projection: TaskNotificationProjection): Notification {
        val builder = Notification.Builder(context, AndroidTaskContinuationPlatform.CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle(context.getString(title(projection.state)))
            .setContentText(context.getString(body(projection.state)))
            .setCategory(Notification.CATEGORY_PROGRESS)
            .setOnlyAlertOnce(true)
            .setOngoing(true)
            .setVisibility(Notification.VISIBILITY_PRIVATE)
            .setPublicVersion(publicVersion())
            .setContentIntent(contentIntent(projection.notificationId))

        if (projection.state == TaskNotificationProjection.State.ACTIVE) {
            val progress = projection.progressPercent
            builder.setProgress(100, progress ?: 0, progress == null)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE)
            }
        }
        projection.actions.forEach { action ->
            builder.addAction(
                Notification.Action.Builder(
                    Icon.createWithResource(context, actionIcon(action)),
                    context.getString(actionLabel(action)),
                    TaskContinuationService.controlPendingIntent(context, projection, action),
                ).build(),
            )
        }
        return builder.build()
    }

    private fun publicVersion(): Notification =
        Notification.Builder(context, AndroidTaskContinuationPlatform.CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle(context.getString(R.string.taffy_task_notification_public_title))
            .setContentText(context.getString(R.string.taffy_task_notification_public_body))
            .setOnlyAlertOnce(true)
            .build()

    private fun contentIntent(notificationId: Int): PendingIntent? {
        val launch = context.packageManager.getLaunchIntentForPackage(context.packageName)
            ?: return null
        return PendingIntent.getActivity(
            context,
            notificationId,
            launch,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
    }

    private fun title(state: TaskNotificationProjection.State): Int = when (state) {
        TaskNotificationProjection.State.ACTIVE -> R.string.taffy_task_notification_active_title
        TaskNotificationProjection.State.WAITING -> R.string.taffy_task_notification_waiting_title
        TaskNotificationProjection.State.PAUSED -> R.string.taffy_task_notification_paused_title
    }

    private fun body(state: TaskNotificationProjection.State): Int = when (state) {
        TaskNotificationProjection.State.ACTIVE -> R.string.taffy_task_notification_active_body
        TaskNotificationProjection.State.WAITING -> R.string.taffy_task_notification_waiting_body
        TaskNotificationProjection.State.PAUSED -> R.string.taffy_task_notification_paused_body
    }

    private fun actionLabel(action: TaskNotificationAction): Int = when (action) {
        TaskNotificationAction.PAUSE -> R.string.taffy_task_notification_pause
        TaskNotificationAction.RESUME -> R.string.taffy_task_notification_resume
        TaskNotificationAction.STOP -> R.string.taffy_task_notification_stop
    }

    private fun actionIcon(action: TaskNotificationAction): Int = when (action) {
        TaskNotificationAction.PAUSE -> android.R.drawable.ic_media_pause
        TaskNotificationAction.RESUME -> android.R.drawable.ic_media_play
        TaskNotificationAction.STOP -> android.R.drawable.ic_menu_close_clear_cancel
    }
}
