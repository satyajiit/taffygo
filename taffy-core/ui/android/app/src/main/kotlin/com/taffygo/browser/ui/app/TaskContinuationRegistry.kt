// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.Closeable

/** Bounded process-local ingress from notification actions to profile authority. */
internal class TaskContinuationRegistry internal constructor(
    private val capacity: Int = DEFAULT_CAPACITY,
) {
    private val lock = Any()
    private val entries = linkedMapOf<String, Entry>()
    private var nextGeneration = 1L

    init {
        require(capacity in 1..MAX_CAPACITY) { "Task notification registry capacity is invalid" }
    }

    fun register(
        profileToken: String,
        handler: (TaskNotificationControlRequest) -> Boolean,
    ): Registration? {
        if (!validIdentity(profileToken, MAX_PROFILE_TOKEN_LENGTH)) return null
        return synchronized(lock) {
            if (entries.size >= capacity || profileToken in entries) return@synchronized null
            val notificationId = (NOTIFICATION_ID_BASE until NOTIFICATION_ID_BASE + capacity)
                .firstOrNull { candidate -> entries.values.none { it.notificationId == candidate } }
                ?: return@synchronized null
            val generation = nextGeneration++
            entries[profileToken] = Entry(generation, notificationId, handler)
            Registration(notificationId) { unregister(profileToken, generation) }
        }
    }

    fun dispatch(request: TaskNotificationControlRequest): Boolean {
        if (!validIdentity(request.profileToken, MAX_PROFILE_TOKEN_LENGTH) ||
            !validIdentity(request.taskId, MAX_TASK_ID_LENGTH)
        ) {
            return false
        }
        val handler = synchronized(lock) { entries[request.profileToken]?.handler } ?: return false
        return handler(request)
    }

    internal fun registrationCount(): Int = synchronized(lock) { entries.size }

    internal fun hasRegistration(profileToken: String): Boolean =
        synchronized(lock) { profileToken in entries }

    private fun unregister(profileToken: String, generation: Long) {
        synchronized(lock) {
            if (entries[profileToken]?.generation == generation) entries.remove(profileToken)
        }
    }

    private data class Entry(
        val generation: Long,
        val notificationId: Int,
        val handler: (TaskNotificationControlRequest) -> Boolean,
    )

    class Registration internal constructor(
        val notificationId: Int,
        private val unregister: () -> Unit,
    ) : Closeable {
        private var closed = false

        @Synchronized
        override fun close() {
            if (closed) return
            closed = true
            unregister()
        }
    }

    companion object {
        val process = TaskContinuationRegistry()

        private fun validIdentity(value: String, maximumLength: Int): Boolean =
            value.isNotBlank() && value.length <= maximumLength && value.none(Char::isISOControl)

        private const val DEFAULT_CAPACITY = 8
        private const val MAX_CAPACITY = 32
        private const val NOTIFICATION_ID_BASE = 48_000
        private const val MAX_PROFILE_TOKEN_LENGTH = 256
        private const val MAX_TASK_ID_LENGTH = 512
    }
}
