// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskProjection
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test
import java.lang.reflect.Modifier
import taffy.core_api.TaskPhase

class TaskNotificationProjectionTest {
    @Test
    fun `only live nonterminal states project and only exact notification controls remain`() {
        val active = projection(
            task(TaskPhase.RUNNING, controls = TaskControl.entries, progress = 3_451u),
        )

        assertEquals(TaskNotificationProjection.State.ACTIVE, active?.state)
        assertEquals(35, active?.progressPercent)
        assertEquals(
            listOf(
                TaskNotificationAction.PAUSE,
                TaskNotificationAction.RESUME,
                TaskNotificationAction.STOP,
            ),
            active?.actions,
        )
        assertFalse(TaskNotificationAction.entries.any { it.control == TaskControl.TAKE_OVER })

        assertEquals(
            TaskNotificationProjection.State.WAITING,
            projection(task(TaskPhase.WAITING_FOR_USER))?.state,
        )
        assertEquals(
            TaskNotificationProjection.State.PAUSED,
            projection(task(TaskPhase.PAUSED))?.state,
        )
        listOf(
            TaskPhase.IDLE,
            TaskPhase.COMPLETED,
            TaskPhase.PARTIAL,
            TaskPhase.CANCELLED,
            TaskPhase.FAILED,
            TaskPhase.OUTCOME_UNKNOWN,
        ).forEach { phase -> assertNull(projection(task(phase))) }
    }

    @Test
    fun `planning is indeterminate and progress is bounded`() {
        assertNull(projection(task(TaskPhase.PLANNING, progress = 8_000u))?.progressPercent)
        assertEquals(
            100,
            projection(task(TaskPhase.RUNNING, progress = UInt.MAX_VALUE))?.progressPercent,
        )
    }

    @Test
    fun `projection never carries task page prompt provider or goal content`() {
        val secret = "seeded-secret-never-notify"
        val projected = projection(
            task(TaskPhase.RUNNING).copy(
                goal = secret,
                statusMessageKey = "$secret-status",
                askPrompt = "$secret-prompt",
            ),
        )

        assertFalse(projected.toString().contains(secret))
        assertEquals(
            setOf(
                "profileToken",
                "notificationId",
                "taskId",
                "taskRevision",
                "state",
                "progressPercent",
                "actions",
            ),
            TaskNotificationProjection::class.java.declaredFields
                .filterNot { Modifier.isStatic(it.modifiers) }
                .map { it.name }
                .filterNot { it.startsWith("$") }
                .toSet(),
        )
    }

    private fun projection(task: TaskProjection?) = task?.let {
        TaskNotificationProjection.from("profile-token", 48_000, it)
    }

    private fun task(
        phase: TaskPhase,
        controls: List<TaskControl> = listOf(TaskControl.STOP),
        progress: UInt = 0u,
    ) = TaskProjection(
        id = "task-id",
        revision = 7u,
        phase = phase,
        goal = "research something",
        template = TaskTemplate.SUMMARIZE_EVIDENCE,
        progressBasisPoints = progress,
        statusMessageKey = null,
        failure = null,
        pendingAction = null,
        allowedControls = controls,
    )
}
