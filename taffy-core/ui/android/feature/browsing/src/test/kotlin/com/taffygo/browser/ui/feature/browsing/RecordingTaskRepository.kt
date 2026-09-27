// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** Records what the box asks of the core and publishes what a test says the core holds. */
internal class RecordingTaskRepository(
    availability: CoreUiAvailability = CoreUiAvailability.READY,
) : TaskRepository {
    /** One start, as the box sent it. */
    data class Start(val goal: String, val template: TaskTemplate, val consent: TaskConsentIntent)

    /** One follow-up, as the box sent it (decision 0137). */
    data class FollowUp(val taskId: String, val question: String)

    val starts = mutableListOf<Start>()
    val skillOfferIds = mutableListOf<String?>()
    val followUps = mutableListOf<FollowUp>()
    var followed: String? = null
        private set
    var startResult: TaffyResult<Unit> = TaffyResult.Success(Unit)
    var followUpResult: TaffyResult<Unit> = TaffyResult.Success(Unit)
    var coreRetries = 0
        private set

    private val status = MutableStateFlow(TaskRepositoryState(availability, 1u, null))
    override val state: StateFlow<TaskRepositoryState> = status

    fun publish(task: TaskProjection) {
        status.value = status.value.copy(task = task)
    }

    override fun follow(taskId: String?) {
        followed = taskId
    }

    override suspend fun startTask(
        goal: String,
        template: TaskTemplate,
        consent: TaskConsentIntent,
        workspaceId: String?,
    ): TaffyResult<Unit> {
        starts += Start(goal, template, consent)
        return startResult
    }

    override suspend fun startTask(
        goal: String,
        template: TaskTemplate,
        consent: TaskConsentIntent,
        workspaceId: String?,
        skillOfferId: String?,
    ): TaffyResult<Unit> {
        skillOfferIds += skillOfferId
        return startTask(goal, template, consent, workspaceId)
    }

    override suspend fun cancelTask(taskId: String) = TaffyResult.Success(Unit)
    override suspend fun completeHandover(taskId: String) = TaffyResult.Success(Unit)
    override suspend fun supplyUserInput(taskId: String, answer: String) = TaffyResult.Success(Unit)

    override suspend fun followUp(taskId: String, question: String): TaffyResult<Unit> {
        followUps += FollowUp(taskId, question)
        return followUpResult
    }

    override suspend fun approveAction(taskId: String, actionId: String) = TaffyResult.Success(Unit)

    override suspend fun retryCore(): TaffyResult<Unit> {
        coreRetries++
        return TaffyResult.Success(Unit)
    }
}
