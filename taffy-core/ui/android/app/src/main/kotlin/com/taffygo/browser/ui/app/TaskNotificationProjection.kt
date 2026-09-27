// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.task.TaskProjection
import taffy.core_api.TaskPhase

/**
 * The complete notification payload, deliberately excluding task content.
 *
 * Goal, page, source, provider, prompt, answer, and status-message text never
 * cross this seam. Android receives only opaque identity, progress, a closed
 * presentation state, and controls admitted by the exact task revision.
 */
internal data class TaskNotificationProjection(
    val profileToken: String,
    val notificationId: Int,
    val taskId: String,
    val taskRevision: ULong,
    val state: State,
    val progressPercent: Int?,
    val actions: List<TaskNotificationAction>,
) {
    val requiresForeground: Boolean
        get() = state == State.ACTIVE

    enum class State {
        ACTIVE,
        WAITING,
        PAUSED,
    }

    companion object {
        fun from(
            profileToken: String,
            notificationId: Int,
            task: TaskProjection,
        ): TaskNotificationProjection? {
            val state = when (task.phase) {
                TaskPhase.PLANNING,
                TaskPhase.RUNNING,
                -> State.ACTIVE
                TaskPhase.WAITING_FOR_USER -> State.WAITING
                TaskPhase.PAUSED -> State.PAUSED
                TaskPhase.IDLE,
                TaskPhase.COMPLETED,
                TaskPhase.PARTIAL,
                TaskPhase.CANCELLED,
                TaskPhase.FAILED,
                TaskPhase.OUTCOME_UNKNOWN,
                -> return null
            }
            val actions = task.allowedControls.mapNotNull(TaskNotificationAction::fromControl)
                .distinct()
            return TaskNotificationProjection(
                profileToken = profileToken,
                notificationId = notificationId,
                taskId = task.id,
                taskRevision = task.revision,
                state = state,
                progressPercent = progress(task, state),
                actions = actions,
            )
        }

        private fun progress(task: TaskProjection, state: State): Int? {
            if (state != State.ACTIVE || task.phase == TaskPhase.PLANNING) return null
            val bounded = task.progressBasisPoints.coerceAtMost(MAX_BASIS_POINTS)
            return ((bounded + HALF_PERCENT_BASIS_POINTS) / BASIS_POINTS_PER_PERCENT).toInt()
        }

        private const val MAX_BASIS_POINTS = 10_000u
        private const val HALF_PERCENT_BASIS_POINTS = 50u
        private const val BASIS_POINTS_PER_PERCENT = 100u
    }
}
