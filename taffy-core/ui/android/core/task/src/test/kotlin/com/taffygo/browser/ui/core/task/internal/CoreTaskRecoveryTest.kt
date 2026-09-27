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
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
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

@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreTaskRecoveryTest {
    @Test
    fun `recovery projection is unavailable hides task and refuses start before dispatch`() =
        runTest {
            val dispatcher = StandardTestDispatcher(testScheduler)
            val core = RecordingTaskCoreApiClient(recoveryStatus())
            val lifetime = lifetime(dispatcher)
            val repository = CoreTaskRepository(core, NoAnalytics, lifetime)
            advanceUntilIdle()

            assertEquals(CoreUiAvailability.UNAVAILABLE, repository.state.value.availability)
            assertNull(repository.state.value.task)

            val result = repository.startTask(
                goal = "hidden goal",
                template = TaskTemplate.WEB_ERRAND,
                consent = TaskConsentIntent(
                    sourceHosts = emptyList(),
                    sourceDiscoveryEnabled = false,
                    newSourceCap = 0,
                    providerRoute = ProviderRoute.NOT_CONFIGURED,
                ),
            )

            assertEquals(FailureReason.CORE_UNAVAILABLE, result.reasonOrNull())
            assertEquals(0, core.startTaskCalls)
            lifetime.close()
        }

    private data object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

private fun recoveryStatus() = CoreStatus(
    availability = CoreAvailability.READY,
    generation = 9uL,
    active_tasks = listOf(
        TaskViewState(
            task_id = "hidden-task",
            revision = 1uL,
            phase = TaskPhase.RUNNING,
            progress_basis_points = 1u,
            status_message_key = "task.acting",
            failure = null,
            goal = "must not project",
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
    projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
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
