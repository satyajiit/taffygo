// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import java.lang.ref.WeakReference

/**
 * Thin Android lifetime and action ingress for an already-authorized task.
 *
 * It neither owns nor restores task state. A killed process is not restarted,
 * and every control must still resolve through the live process registry.
 */
class TaskContinuationService : Service() {
    private val prepared = linkedMapOf<String, TaskNotificationProjection>()
    private val foregroundProfiles = linkedSetOf<String>()
    private var foregroundProfile: String? = null
    private val notificationFactory by lazy(LazyThreadSafetyMode.NONE) {
        TaskNotificationFactory(this)
    }

    override fun onCreate() {
        super.onCreate()
        live = WeakReference(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_PREPARE -> prepare(intent.toProjection())
            ACTION_ACTIVATE -> activate(intent.toProjection())
            ACTION_CONTROL -> dispatch(intent.toControlRequest())
        }
        stopIfIdle(startId)
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        val stranded = foregroundProfiles.mapNotNull(prepared::get)
        prepared.clear()
        foregroundProfiles.clear()
        foregroundProfile = null
        if (live?.get() === this) live = null
        stranded.forEach(::pauseAndCancel)
        super.onDestroy()
    }

    private fun prepare(projection: TaskNotificationProjection?) {
        if (projection == null || !projection.requiresForeground ||
            !TaskContinuationRegistry.process.hasRegistration(projection.profileToken)
        ) {
            return
        }
        prepared[projection.profileToken] = projection
        if (foregroundProfiles.remove(projection.profileToken) &&
            foregroundProfile == projection.profileToken
        ) {
            if (!promoteLatest()) {
                stopForeground(STOP_FOREGROUND_DETACH)
                foregroundProfile = null
            }
        }
    }

    private fun activate(projection: TaskNotificationProjection?) {
        if (projection == null || !projection.requiresForeground ||
            !TaskContinuationRegistry.process.hasRegistration(projection.profileToken)
        ) {
            return
        }
        prepared[projection.profileToken] = projection
        val newlyForeground = foregroundProfiles.add(projection.profileToken)
        if (!newlyForeground && foregroundProfile != null) return
        if (!promote(projection)) {
            prepared.remove(projection.profileToken)
            foregroundProfiles.remove(projection.profileToken)
            pauseAndCancel(projection)
            promoteLatest()
        }
    }

    private fun dispatch(request: TaskNotificationControlRequest?) {
        if (request != null) TaskContinuationRegistry.process.dispatch(request)
    }

    private fun changeForeground(
        profileToken: String,
        removeNotification: Boolean,
    ) {
        val removed = prepared.remove(profileToken)
        foregroundProfiles.remove(profileToken)
        val wasForeground = foregroundProfile == profileToken
        val replaced = !wasForeground || promoteLatest()
        when {
            replaced -> Unit
            removed != null && removeNotification -> stopForeground(STOP_FOREGROUND_REMOVE)
            removed != null -> stopForeground(STOP_FOREGROUND_DETACH)
        }
        if (prepared.isEmpty()) stopSelf()
    }

    private fun promoteLatest(): Boolean {
        while (true) {
            val profileToken = foregroundProfiles.lastOrNull() ?: return false
            val projection = prepared[profileToken]
            if (projection == null) {
                foregroundProfiles.remove(profileToken)
                continue
            }
            if (promote(projection)) return true
            prepared.remove(projection.profileToken)
            foregroundProfiles.remove(projection.profileToken)
            pauseAndCancel(projection)
        }
    }

    private fun promote(projection: TaskNotificationProjection): Boolean = try {
        val notification = notificationFactory.build(projection)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(
                projection.notificationId,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE,
            )
        } else {
            startForeground(projection.notificationId, notification)
        }
        foregroundProfile = projection.profileToken
        true
    } catch (_: RuntimeException) {
        false
    }

    private fun pauseAndCancel(projection: TaskNotificationProjection) {
        getSystemService(NotificationManager::class.java)?.cancel(projection.notificationId)
        if (TaskNotificationAction.PAUSE in projection.actions) {
            TaskContinuationRegistry.process.dispatch(
                TaskNotificationControlRequest(
                    projection.profileToken,
                    projection.taskId,
                    projection.taskRevision,
                    TaskNotificationAction.PAUSE,
                ),
            )
        }
    }

    private fun stopIfIdle(startId: Int) {
        if (prepared.isEmpty()) stopSelfResult(startId)
    }

    companion object {
        private const val ACTION_PREPARE = "com.taffygo.browser.task.PREPARE"
        private const val ACTION_ACTIVATE = "com.taffygo.browser.task.ACTIVATE"
        private const val ACTION_CONTROL = "com.taffygo.browser.task.CONTROL"
        private const val EXTRA_PROFILE = "profile"
        private const val EXTRA_NOTIFICATION_ID = "notification_id"
        private const val EXTRA_TASK = "task"
        private const val EXTRA_REVISION = "revision"
        private const val EXTRA_STATE = "state"
        private const val EXTRA_PROGRESS = "progress"
        private const val EXTRA_ACTIONS = "actions"
        private const val EXTRA_ACTION = "action"
        private const val NO_PROGRESS = -1
        private val main = Handler(Looper.getMainLooper())

        @Volatile
        private var live: WeakReference<TaskContinuationService>? = null

        internal fun activationIntent(
            context: Context,
            projection: TaskNotificationProjection,
        ): Intent = Intent(context, TaskContinuationService::class.java)
            .setAction(ACTION_ACTIVATE)
            .putProjection(projection)

        internal fun preparationIntent(
            context: Context,
            projection: TaskNotificationProjection,
        ): Intent = Intent(context, TaskContinuationService::class.java)
            .setAction(ACTION_PREPARE)
            .putProjection(projection)

        internal fun prepareLive(projection: TaskNotificationProjection): Boolean {
            val service = live?.get() ?: return false
            main.post {
                if (live?.get() === service) service.prepare(projection)
            }
            return true
        }

        internal fun promotePrepared(projection: TaskNotificationProjection): Boolean {
            val service = live?.get() ?: return false
            main.post {
                if (live?.get() === service) {
                    service.activate(projection)
                } else if (TaskNotificationAction.PAUSE in projection.actions) {
                    TaskContinuationRegistry.process.dispatch(
                        TaskNotificationControlRequest(
                            projection.profileToken,
                            projection.taskId,
                            projection.taskRevision,
                            TaskNotificationAction.PAUSE,
                        ),
                    )
                }
            }
            return true
        }

        internal fun controlPendingIntent(
            context: Context,
            projection: TaskNotificationProjection,
            action: TaskNotificationAction,
        ): android.app.PendingIntent {
            val request = TaskNotificationControlRequest(
                projection.profileToken,
                projection.taskId,
                projection.taskRevision,
                action,
            )
            return android.app.PendingIntent.getService(
                context,
                projection.notificationId * ACTION_REQUEST_STRIDE + action.ordinal,
                Intent(context, TaskContinuationService::class.java)
                    .setAction(ACTION_CONTROL)
                    .setIdentifier(request.pendingIntentIdentifier())
                    .putExtra(EXTRA_PROFILE, request.profileToken)
                    .putExtra(EXTRA_TASK, request.taskId)
                    .putExtra(EXTRA_REVISION, request.taskRevision.toString())
                    .putExtra(EXTRA_ACTION, request.action.wireName),
                android.app.PendingIntent.FLAG_IMMUTABLE or
                    android.app.PendingIntent.FLAG_CANCEL_CURRENT,
            )
        }

        internal fun releaseForeground(
            profileToken: String,
            removeNotification: Boolean,
        ) {
            val service = live?.get() ?: return
            main.post {
                if (live?.get() === service) {
                    service.changeForeground(profileToken, removeNotification)
                }
            }
        }

        private const val ACTION_REQUEST_STRIDE = 8

        private fun Intent.putProjection(projection: TaskNotificationProjection): Intent =
            putExtra(EXTRA_PROFILE, projection.profileToken)
                .putExtra(EXTRA_NOTIFICATION_ID, projection.notificationId)
                .putExtra(EXTRA_TASK, projection.taskId)
                .putExtra(EXTRA_REVISION, projection.taskRevision.toString())
                .putExtra(EXTRA_STATE, projection.state.name)
                .putExtra(EXTRA_PROGRESS, projection.progressPercent ?: NO_PROGRESS)
                .putExtra(EXTRA_ACTIONS, projection.actions.joinToString(",") { it.wireName })

        private fun Intent.toProjection(): TaskNotificationProjection? {
            val profile = getStringExtra(EXTRA_PROFILE)?.takeIf(String::isNotBlank) ?: return null
            val task = getStringExtra(EXTRA_TASK)?.takeIf(String::isNotBlank) ?: return null
            val revision = getStringExtra(EXTRA_REVISION)?.toULongOrNull() ?: return null
            val notificationId = getIntExtra(EXTRA_NOTIFICATION_ID, 0).takeIf { it > 0 }
                ?: return null
            val state = getStringExtra(EXTRA_STATE)?.let { encoded ->
                TaskNotificationProjection.State.entries.firstOrNull { it.name == encoded }
            } ?: return null
            val progress = getIntExtra(EXTRA_PROGRESS, NO_PROGRESS).takeIf { it in 0..100 }
            val actions = getStringExtra(EXTRA_ACTIONS).orEmpty()
                .split(',')
                .filter(String::isNotEmpty)
                .mapNotNull(TaskNotificationAction::fromWireName)
                .distinct()
            return TaskNotificationProjection(
                profile,
                notificationId,
                task,
                revision,
                state,
                progress,
                actions,
            )
        }

        private fun Intent.toControlRequest(): TaskNotificationControlRequest? {
            val profile = getStringExtra(EXTRA_PROFILE) ?: return null
            val task = getStringExtra(EXTRA_TASK) ?: return null
            val revision = getStringExtra(EXTRA_REVISION)?.toULongOrNull() ?: return null
            val action = getStringExtra(EXTRA_ACTION)?.let(TaskNotificationAction::fromWireName)
                ?: return null
            return TaskNotificationControlRequest(profile, task, revision, action)
        }
    }
}
