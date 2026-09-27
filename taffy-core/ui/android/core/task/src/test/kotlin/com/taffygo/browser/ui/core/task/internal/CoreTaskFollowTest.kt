// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
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
 * The repository follows one task and exposes every task.
 *
 * The core can hold more than one session — a finished one it still lists
 * beside a start that was just admitted — and projecting `active_tasks.first()`
 * made the new one invisible for as long as the old one sat ahead of it. That
 * is the shape the phone showed as "nothing was started".
 */
@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreTaskFollowTest {
    @Test
    fun `with nothing followed the task under way wins over a finished one listed first`() =
        runTest {
            val dispatcher = StandardTestDispatcher(testScheduler)
            val repository = CoreTaskRepository(
                RecordingTaskCoreApiClient(twoTasks()),
                NoAnalytics,
                lifetime(dispatcher),
            )
            advanceUntilIdle()

            val state = repository.state.value
            // The followed task leads the list; the rest keep the core's order.
            assertEquals(listOf("fresh", "finished"), state.tasks.map { it.id })
            assertEquals("fresh", state.task?.id)
        }

    @Test
    fun `following names the task the surfaces read`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val repository = CoreTaskRepository(
            RecordingTaskCoreApiClient(twoTasks()),
            NoAnalytics,
            lifetime(dispatcher),
        )
        advanceUntilIdle()

        repository.follow("finished")
        advanceUntilIdle()
        assertEquals("finished", repository.state.value.task?.id)

        // An id the core no longer holds falls back to the under-way rule
        // rather than to nothing: a surface is never left with no task while
        // one is running.
        repository.follow("gone")
        advanceUntilIdle()
        assertEquals("fresh", repository.state.value.task?.id)
        assertEquals(2, repository.state.value.tasks.size)
    }

    private object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

private fun task(id: String, phase: TaskPhase) = TaskViewState(
    task_id = id,
    revision = 1uL,
    phase = phase,
    progress_basis_points = 0u,
    status_message_key = null,
    failure = null,
    goal = "goal $id",
    template_id = TaskTemplateId.WEB_ERRAND,
    pending_action = null,
    workspace_id = null,
    pending_ask_prompt = null,
    pending_field_value_request = null,
    allowed_controls = emptyList(),
    artifacts = emptyList(),
    activity = emptyList(),
)

private fun twoTasks() = CoreStatus(
    availability = CoreAvailability.READY,
    generation = 3uL,
    active_tasks = listOf(
        task("finished", TaskPhase.COMPLETED),
        task("fresh", TaskPhase.PLANNING),
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
