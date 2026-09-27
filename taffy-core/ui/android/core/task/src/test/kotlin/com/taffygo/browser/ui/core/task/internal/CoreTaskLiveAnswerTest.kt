// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.api.TaskAnswerReport
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.AssistantConfigurationView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.LibraryAvailability
import taffy.core_api.LibraryViewState
import taffy.core_api.MemoryAvailability
import taffy.core_api.MemoryViewState
import taffy.core_api.PersonalityPresetView
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedDetailsView
import taffy.core_api.SavedSignInsView
import taffy.core_api.TaskPhase
import taffy.core_api.TaskTemplateId
import taffy.core_api.TaskViewState

/**
 * A live answer belongs to the generation that spoke it (decision 0168).
 *
 * The phone's shape: a task paused mid-thought, the utility process gone, and
 * the task restored Paused. The call the old generation was holding is given
 * up in `Reducer::replay` and emits no terminal, so this process keeps the
 * last streaming projection it received and screen SCR-303 says "Taffy is
 * answering…" for the life of the browser run, with the footer controls
 * behind it.
 */
@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreTaskLiveAnswerTest {
    @Test
    fun `a call the gone generation was speaking stops claiming more is coming`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingTaskCoreApiClient(pausedTask(generation = 3uL))
        val repository = CoreTaskRepository(core, NoAnalytics, lifetime(dispatcher))
        advanceUntilIdle()

        core.answers.emit(delta("Reading the form"))
        advanceUntilIdle()
        assertTrue(repository.state.value.task?.liveAnswer?.isStreaming == true)

        // The utility process went and the browser's counter advanced. The
        // terminal for that call can never arrive now.
        core.statuses.value = pausedTask(generation = 4uL)
        advanceUntilIdle()

        val answer = repository.state.value.task?.liveAnswer
        assertFalse(answer?.isStreaming == true)
        assertTrue(answer?.isIncomplete == true)
        // The text stays. A person read it, and it was real.
        assertEquals(listOf("Reading the form"), answer?.segments)
    }

    @Test
    fun `a call that finished before the generation went is left alone`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingTaskCoreApiClient(pausedTask(generation = 3uL))
        val repository = CoreTaskRepository(core, NoAnalytics, lifetime(dispatcher))
        advanceUntilIdle()

        core.answers.emit(delta("All done"))
        core.answers.emit(terminal())
        advanceUntilIdle()

        core.statuses.value = pausedTask(generation = 4uL)
        advanceUntilIdle()

        val answer = repository.state.value.task?.liveAnswer
        assertFalse(answer?.isStreaming == true)
        // The caution belongs to a call that was cut off, and this one was not.
        assertFalse(answer?.isIncomplete == true)
        assertEquals(listOf("All done"), answer?.segments)
    }

    @Test
    fun `a call the running generation is still speaking is still answering`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingTaskCoreApiClient(pausedTask(generation = 3uL))
        val repository = CoreTaskRepository(core, NoAnalytics, lifetime(dispatcher))
        advanceUntilIdle()

        core.answers.emit(delta("Reading the form"))
        advanceUntilIdle()

        // A second publication from the same generation: the task moved on,
        // and nothing about the call it is making changed.
        core.statuses.value = pausedTask(generation = 3uL, revision = 2uL)
        advanceUntilIdle()

        val answer = repository.state.value.task?.liveAnswer
        assertTrue(answer?.isStreaming == true)
        assertFalse(answer?.isIncomplete == true)
    }

    private object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

private const val TASK_ID = "errand"

private fun delta(text: String) = TaskAnswerReport(
    taskId = TASK_ID,
    callId = "call-1",
    sequence = 0u,
    text = text,
    terminal = false,
    complete = false,
)

private fun terminal() = TaskAnswerReport(
    taskId = TASK_ID,
    callId = "call-1",
    sequence = 1u,
    text = null,
    terminal = true,
    complete = true,
)

private fun pausedTask(generation: ULong, revision: ULong = 1uL) = CoreStatus(
    availability = CoreAvailability.READY,
    generation = generation,
    active_tasks = listOf(
        TaskViewState(
            task_id = TASK_ID,
            revision = revision,
            phase = TaskPhase.PAUSED,
            progress_basis_points = 0u,
            status_message_key = "task.paused",
            failure = null,
            goal = "download my eAadhaar",
            template_id = TaskTemplateId.WEB_ERRAND,
            pending_action = null,
            workspace_id = null,
            pending_ask_prompt = null,
            pending_field_value_request = null,
            allowed_controls = emptyList(),
            artifacts = emptyList(),
            activity = emptyList(),
        ),
    ),
    auth_state = null,
    workspaces = emptyList(),
    workspace_export = null,
    asset_delivery = null,
    provider_roster = emptyList(),
    provider_probes = emptyList(),
    provider_models = emptyList(),
    assistant_configuration = AssistantConfigurationView(
        revision = 0uL,
        disabled_abilities = emptyList(),
        preset = PersonalityPresetView.CAREFUL_RESEARCHER,
        pace = 0u,
        length = 1u,
        check_in = 0u,
    ),
    library = LibraryViewState(
        availability = LibraryAvailability.AVAILABLE,
        revision = 0uL,
        entries = emptyList(),
        search = null,
        refresh_previews = emptyList(),
        refresh_results = emptyList(),
    ),
    library_export = null,
    memory = MemoryViewState(
        availability = MemoryAvailability.AVAILABLE,
        revision = 0uL,
        records = emptyList(),
        search = null,
    ),
    saved_sign_ins = SavedSignInsView(
        availability = SavedDataAvailability.UNAVAILABLE,
        revision = 0uL,
        records = emptyList(),
    ),
    saved_details = SavedDetailsView(
        availability = SavedDataAvailability.UNAVAILABLE,
        revision = 0uL,
        people = emptyList(),
    ),
    site_skills = emptyList(),
    builtin_skills = emptyList(),
    projection_mode = CoreStatusProjectionMode.COMPLETE,
    projection_omissions = emptyList(),
)

private fun lifetime(dispatcher: CoroutineDispatcher) = TaffyProfileLifetime(
    dispatchers = object : AppDispatchers {
        override val main = dispatcher
        override val default = dispatcher
        override val io = dispatcher
    },
    failureSink = CoroutineFailureSink { },
)
