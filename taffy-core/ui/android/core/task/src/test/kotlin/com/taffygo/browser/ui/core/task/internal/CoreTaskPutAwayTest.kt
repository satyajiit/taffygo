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
import org.junit.Assert.assertNull
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
 * A person can be done with a task that has ended.
 *
 * Nothing retires a terminal task for the rest of a browser run: the core goes
 * on publishing it, the bar goes on showing its red pill, and no command at any
 * seam dismisses one. This is the residency answer to that — the core is never
 * told, so the session stays held with everything it is still owed (decision
 * 0149 clause 5), and a filter can only ever show less than was published.
 */
@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreTaskPutAwayTest {
    @Test
    fun `an ended task put away leaves the surfaces`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingTaskCoreApiClient(
            statusOf(task("failed", TaskPhase.FAILED)),
        )
        val repository = CoreTaskRepository(core, NoAnalytics, lifetime(dispatcher))
        advanceUntilIdle()
        assertEquals("failed", repository.state.value.task?.id)

        repository.putAway("failed")
        advanceUntilIdle()

        assertNull(repository.state.value.task)
        assertTrue(repository.state.value.tasks.isEmpty())
    }

    @Test
    fun `a running task cannot be put away`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingTaskCoreApiClient(
            statusOf(task("running", TaskPhase.RUNNING)),
        )
        val repository = CoreTaskRepository(core, NoAnalytics, lifetime(dispatcher))
        advanceUntilIdle()

        repository.putAway("running")
        advanceUntilIdle()

        assertEquals("running", repository.state.value.task?.id)
    }

    @Test
    fun `a paused task cannot be put away, because the way out of a pause is Resume`() =
        runTest {
            val dispatcher = StandardTestDispatcher(testScheduler)
            val core = RecordingTaskCoreApiClient(
                statusOf(task("paused", TaskPhase.PAUSED)),
            )
            val repository = CoreTaskRepository(core, NoAnalytics, lifetime(dispatcher))
            advanceUntilIdle()

            repository.putAway("paused")
            advanceUntilIdle()

            assertEquals("paused", repository.state.value.task?.id)
        }

    @Test
    fun `putting away the followed task does not re-select it`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingTaskCoreApiClient(
            statusOf(
                task("failed", TaskPhase.FAILED),
                task("done", TaskPhase.COMPLETED),
            ),
        )
        val repository = CoreTaskRepository(core, NoAnalytics, lifetime(dispatcher))
        repository.follow("failed")
        advanceUntilIdle()
        assertEquals("failed", repository.state.value.task?.id)

        repository.putAway("failed")
        advanceUntilIdle()

        // This is the assertion that catches `follow(null)` being mistaken for
        // a fix: `followedTask` falls through to the first task there is, and
        // without the filter that fall-through re-selects the same task.
        assertEquals("done", repository.state.value.task?.id)
        assertEquals(listOf("done"), repository.state.value.tasks.map { it.id })
    }

    @Test
    fun `a task the core republishes as running comes straight back`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingTaskCoreApiClient(
            statusOf(task("resumed", TaskPhase.CANCELLED)),
        )
        val repository = CoreTaskRepository(core, NoAnalytics, lifetime(dispatcher))
        advanceUntilIdle()

        repository.putAway("resumed")
        advanceUntilIdle()
        assertNull(repository.state.value.task)

        core.statuses.value = statusOf(task("resumed", TaskPhase.RUNNING, revision = 2uL))
        advanceUntilIdle()

        assertEquals("resumed", repository.state.value.task?.id)
    }

    private object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

private fun task(id: String, phase: TaskPhase, revision: ULong = 1uL) = TaskViewState(
    task_id = id,
    revision = revision,
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

private fun statusOf(vararg tasks: TaskViewState) = CoreStatus(
    availability = CoreAvailability.READY,
    generation = 3uL,
    active_tasks = tasks.toList(),
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
