// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emptyFlow
import taffy.core_api.TaskConsentPreview
import taffy.core_api.TaskTemplateId

/**
 * One task's life as the surfaces steer it: starting it, stopping it, handing
 * the page back and forth, and the visible text it produced.
 */
interface TaskLifecycleCoreApiClient {
    /**
     * Visible task-answer text as the isolated core decodes it.
     *
     * This flow has no replay and no durable owner. The sandbox has already
     * removed raw provider framing and reasoning fields; snapshots remain the
     * authority for task state and workspace facts. A default empty flow keeps
     * narrow test doubles narrow, while the shipping browser endpoint
     * overrides this property and its seam test requires that override.
     */
    val taskAnswer: Flow<TaskAnswerReport>
        get() = emptyFlow()

    suspend fun startTask(
        goal: String,
        templateId: TaskTemplateId,
        consentPreview: TaskConsentPreview,
        workspaceId: String? = null,
    )

    /** Starts from one explicitly selected opaque saved-skill offer. */
    suspend fun startTask(
        goal: String,
        templateId: TaskTemplateId,
        consentPreview: TaskConsentPreview,
        workspaceId: String?,
        skillOfferId: String?,
    ) {
        check(skillOfferId == null) { "This Core API client cannot submit saved-skill offers" }
        startTask(goal, templateId, consentPreview, workspaceId)
    }
    suspend fun cancelTask(taskId: String)
    suspend fun pauseTask(taskId: String)
    suspend fun resumeTask(taskId: String)
    suspend fun takeOver(taskId: String)

    /** Tell Taffy the person has finished with the handed-over page. */
    suspend fun completeHandover(taskId: String)

    /** Submit the person's answer to a question Taffy asked. */
    suspend fun supplyUserInput(taskId: String, answer: String)

    /**
     * Ask a finished task the next question, on the transcript it already
     * holds. The words cross the seam the way an answer does and are never
     * journaled; a task whose transcript is gone refuses the command.
     */
    suspend fun followUp(taskId: String, question: String)

    suspend fun approveAction(taskId: String, actionId: String)
}
