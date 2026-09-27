// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.common.di.TaffyProfileIdentity
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepository
import java.io.Closeable
import javax.inject.Inject
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Profile-owned policy for one content-free task notification.
 *
 * The task repository remains the only task authority. This controller merely
 * projects it, tracks whether this profile has a resumed window, and routes an
 * exact notification action back through [TaskRepository.useControl].
 */
@TaffyProfileScope
class TaskContinuationController @Inject internal constructor(
    private val repository: TaskRepository,
    private val preferences: UserPreferencesRepository,
    private val identity: TaffyProfileIdentity,
    private val lifetime: TaffyProfileLifetime,
    private val registry: TaskContinuationRegistry,
    private val platform: TaskContinuationPlatform,
) : Closeable {
    private val lock = Any()
    private val inputLock = Mutex()
    private val visibleWindows = MutableStateFlow<Set<String>>(emptySet())
    private var registration: TaskContinuationRegistry.Registration? = null
    private var collection: Job? = null
    private var backgroundSettlement: Job? = null
    private var latestInput: Input? = null
    private var backgroundSettled = false
    private var published = false
    private var lastPause: PauseKey? = null
    private var started = false
    private var closed = false

    /** Start exactly once with the regular profile, before its first window. */
    fun start() {
        synchronized(lock) {
            if (identity.private) return
            check(!closed) { "A closed task continuation cannot start" }
            if (started) return
            started = true
            registration = registry.register(identity.value, ::dispatchControl)
            lifetime.own(this)
            collection = lifetime.scope.launch { collectInputs() }
        }
    }

    /** Follow one exact Chromium window's resumed lifetime. */
    fun setWindowVisible(windowToken: String, visible: Boolean) {
        if (windowToken.isBlank()) return
        synchronized(lock) { if (closed) return }
        visibleWindows.update { current ->
            if (visible) current + windowToken else current - windowToken
        }
    }

    override fun close() {
        val (owned, notificationId) = synchronized(lock) {
            if (closed) return
            closed = true
            Pair(
                listOfNotNull(backgroundSettlement, collection, registration),
                registration?.notificationId,
            ).also {
                backgroundSettlement = null
                collection = null
                registration = null
            }
        }
        owned.forEach { owner ->
            when (owner) {
                is Job -> owner.cancel()
                is Closeable -> owner.close()
            }
        }
        notificationId?.let { platform.remove(identity.value, it) }
        published = false
    }

    private suspend fun collectInputs() {
        val task = repository.state
            .map { it.task }
            .distinctUntilChangedBy { it?.notificationVersion() }
        val enabled = preferences.preferences
            .map { value ->
                value.loaded && NotificationTopic.TASK_PROGRESS in value.notificationTopics
            }
            .distinctUntilChanged()
        val visible = visibleWindows.map(Set<String>::isNotEmpty).distinctUntilChanged()
        combine(task, enabled, visible, ::Input).collect { acceptInput(it) }
    }

    private suspend fun acceptInput(input: Input) {
        inputLock.withLock {
            latestInput = input
            if (input.visible) {
                backgroundSettlement?.cancel()
                backgroundSettlement = null
                backgroundSettled = false
                apply(input)
                return
            }
            if (backgroundSettled) {
                apply(input)
                return
            }
            if (backgroundSettlement?.isActive == true) return
            backgroundSettlement = lifetime.scope.launch {
                delay(BACKGROUND_SETTLEMENT_MS)
                inputLock.withLock {
                    backgroundSettled = true
                    latestInput?.takeUnless(Input::visible)?.let { apply(it) }
                }
            }
        }
    }

    private suspend fun apply(input: Input) {
        val registration = registration
        if (registration == null) {
            removeNotification()
            pauseInvisibleContinuation(input)
            return
        }
        val projection = registration.let { activeRegistration ->
            input.task?.let { task ->
                TaskNotificationProjection.from(
                    identity.value,
                    activeRegistration.notificationId,
                    task,
                )
            }
        }
        if (projection == null) {
            removeNotification()
            lastPause = null
            return
        }

        val canPublish = input.notificationEnabled && platform.canPostNotifications()
        if (!canPublish) {
            removeNotification()
            pauseInvisibleContinuation(input)
            return
        }

        val requiresForeground = projection.requiresForeground && !input.visible
        if (platform.publish(projection, requiresForeground)) {
            published = true
            if (input.visible) lastPause = null
        } else {
            published = false
            pauseInvisibleContinuation(input)
        }
    }

    private suspend fun pauseInvisibleContinuation(input: Input) {
        val task = input.task
        if (input.visible || task == null || !task.isExecuting() ||
            TaskControl.PAUSE !in task.allowedControls
        ) {
            return
        }
        val key = PauseKey(task.id, task.revision)
        if (lastPause == key) return
        lastPause = key
        repository.useControl(key.taskId, key.revision, TaskControl.PAUSE)
    }

    private fun dispatchControl(request: TaskNotificationControlRequest): Boolean {
        val task = repository.state.value.task
        if (task?.id != request.taskId || task.revision != request.taskRevision ||
            request.action.control !in task.allowedControls
        ) {
            return false
        }
        lifetime.scope.launch {
            repository.useControl(
                request.taskId,
                request.taskRevision,
                request.action.control,
            )
        }
        return true
    }

    private fun removeNotification() {
        val notificationId = registration?.notificationId ?: return
        if (!published) return
        platform.remove(identity.value, notificationId)
        published = false
    }

    private data class Input(
        val task: TaskProjection?,
        val notificationEnabled: Boolean,
        val visible: Boolean,
    )

    private data class PauseKey(val taskId: String, val revision: ULong)

    private data class NotificationVersion(
        val id: String,
        val revision: ULong,
        val phase: taffy.core_api.TaskPhase,
        val progressBasisPoints: UInt,
        val controls: List<TaskControl>,
    )

    private fun TaskProjection.notificationVersion() = NotificationVersion(
        id = id,
        revision = revision,
        phase = phase,
        progressBasisPoints = progressBasisPoints,
        controls = allowedControls,
    )

    private fun TaskProjection.isExecuting(): Boolean = when (phase) {
        taffy.core_api.TaskPhase.PLANNING,
        taffy.core_api.TaskPhase.RUNNING,
        -> true
        else -> false
    }

    private companion object {
        const val BACKGROUND_SETTLEMENT_MS = 500L
    }
}
