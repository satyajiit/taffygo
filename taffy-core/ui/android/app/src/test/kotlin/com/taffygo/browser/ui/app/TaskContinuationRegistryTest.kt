// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TaskContinuationRegistryTest {
    @Test
    fun `pending intent identity includes every exact control coordinate`() {
        val request = TaskNotificationControlRequest(
            "profile-a",
            "task-a",
            19u,
            TaskNotificationAction.PAUSE,
        )
        val identities = listOf(
            request,
            request.copy(profileToken = "profile-b"),
            request.copy(taskId = "task-b"),
            request.copy(taskRevision = 20u),
            request.copy(action = TaskNotificationAction.STOP),
        ).map(TaskNotificationControlRequest::pendingIntentIdentifier)

        assertEquals(identities.size, identities.toSet().size)
    }

    @Test
    fun `registration is bounded and notification ids are distinct`() {
        val registry = TaskContinuationRegistry(capacity = 2)
        val first = registry.register("profile-a") { true }
        val second = registry.register("profile-b") { true }

        assertNotNull(first)
        assertNotNull(second)
        assertNotEquals(first?.notificationId, second?.notificationId)
        assertNull(registry.register("profile-c") { true })
        assertNull(registry.register("profile-a") { true })
        assertEquals(2, registry.registrationCount())
    }

    @Test
    fun `exact request reaches only its live profile registration`() {
        val registry = TaskContinuationRegistry(capacity = 2)
        val received = mutableListOf<TaskNotificationControlRequest>()
        val registration = registry.register("profile-a") { request ->
            received += request
            true
        }
        val request = TaskNotificationControlRequest(
            "profile-a",
            "task-a",
            19u,
            TaskNotificationAction.PAUSE,
        )

        assertTrue(registry.dispatch(request))
        assertEquals(listOf(request), received)
        assertFalse(registry.dispatch(request.copy(profileToken = "profile-b")))

        registration?.close()
        assertFalse(registry.dispatch(request))
        assertFalse(registry.hasRegistration("profile-a"))
    }

    @Test
    fun `blank oversized and control-character identities fail closed`() {
        val registry = TaskContinuationRegistry(capacity = 1)
        assertNull(registry.register("") { true })
        assertNull(registry.register("p".repeat(257)) { true })
        assertNull(registry.register("profile\u0000") { true })

        registry.register("profile") { true }
        assertFalse(
            registry.dispatch(
                TaskNotificationControlRequest(
                    "profile",
                    "task\nname",
                    1u,
                    TaskNotificationAction.STOP,
                ),
            ),
        )
    }
}
