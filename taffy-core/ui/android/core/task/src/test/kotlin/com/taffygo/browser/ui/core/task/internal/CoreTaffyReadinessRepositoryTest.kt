// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaffyReadiness.NotSetUp
import com.taffygo.browser.ui.core.task.TaffyReadiness.Ready
import com.taffygo.browser.ui.core.task.TaffyReadiness.RouteChosenButNothingBehindIt
import com.taffygo.browser.ui.core.task.TaffyReadiness.Unknown
import com.taffygo.browser.ui.core.task.taffyReadiness
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test
import taffy.core_api.AssistantConfigurationView
import taffy.core_api.AuthViewState
import taffy.core_api.CatalogLayerView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.LibraryAvailability
import taffy.core_api.LibraryViewState
import taffy.core_api.MemoryAvailability
import taffy.core_api.MemoryViewState
import taffy.core_api.PersonalityPresetView
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderOriginView
import taffy.core_api.ProviderRosterEntry
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedDetailsView
import taffy.core_api.SavedSignInsView
import taffy.core_api.StoredCredentialView

@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreTaffyReadinessRepositoryTest {
    private val direct = ProviderRoute.DIRECT_WITH_YOUR_KEY

    @Test
    fun `unknown until the core publishes a complete projection`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = PublishingCoreApiClient(status(projectionMode = CoreStatusProjectionMode.RECOVERY_REQUIRED))
        val repository = repository(core, dispatcher)
        assertEquals(Unknown, repository.readiness.value)

        core.publish(status())
        advanceUntilIdle()
        assertEquals(NotSetUp, repository.readiness.value)
    }

    @Test
    fun `a republished roster moves the verdict`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = PublishingCoreApiClient(status())
        val repository = repository(core, dispatcher)
        assertEquals(NotSetUp, repository.readiness.value)

        core.publish(status(roster = listOf(entry("openai", stored = credential(ProviderCredentialStateView.USABLE)))))
        advanceUntilIdle()
        assertEquals(Ready(direct), repository.readiness.value)

        core.publish(status(roster = listOf(entry("openai", stored = credential(ProviderCredentialStateView.REFRESH_FAILED)))))
        advanceUntilIdle()
        assertEquals(RouteChosenButNothingBehindIt(direct), repository.readiness.value)
    }

    @Test
    fun `a disabled row counts for nothing`() {
        val facts = status(
            roster = listOf(entry("openai", stored = credential(ProviderCredentialStateView.USABLE), enabled = false)),
        ).toReadinessFacts()
        assertEquals(emptySet<String>(), facts.usableCredentialProviderIds)
        assertEquals(emptySet<String>(), facts.storedCredentialProviderIds)
    }

    @Test
    fun `a key that needs sign-in is stored but not usable`() {
        val facts = status(
            roster = listOf(entry("openai", stored = credential(ProviderCredentialStateView.NEEDS_SIGN_IN))),
        ).toReadinessFacts()
        assertEquals(emptySet<String>(), facts.usableCredentialProviderIds)
        assertEquals(setOf("openai"), facts.storedCredentialProviderIds)
    }

    @Test
    fun `an own address is a custom provider or a repointed catalog one`() {
        val facts = status(
            roster = listOf(
                entry("my-server", origin = ProviderOriginView.CUSTOM),
                entry("openai", layer = CatalogLayerView.USER_OVERRIDE),
                entry("anthropic"),
            ),
        ).toReadinessFacts()
        assertEquals(setOf("my-server", "openai"), facts.ownEndpointProviderIds)
    }

    @Test
    fun `a held handle the core has not confirmed is pending`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val held = MutableStateFlow(setOf("openai"))
        val repository = repository(PublishingCoreApiClient(status()), dispatcher, held = held)
        assertEquals(RouteChosenButNothingBehindIt(direct), repository.readiness.value)

        held.value = emptySet()
        advanceUntilIdle()
        assertEquals(NotSetUp, repository.readiness.value)
    }

    @Test
    fun `the chosen route is read live`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val route = MutableStateFlow(ProviderRoute.NOT_CONFIGURED)
        val core = PublishingCoreApiClient(status())
        val repository = repository(core, dispatcher, route = route)
        assertEquals(NotSetUp, repository.readiness.value)

        route.value = direct
        advanceUntilIdle()
        assertEquals(RouteChosenButNothingBehindIt(direct), repository.readiness.value)
    }

    @Test
    fun `an unrelated status change does not move the verdict`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val roster = listOf(entry("openai", stored = credential(ProviderCredentialStateView.USABLE)))
        val core = PublishingCoreApiClient(status(roster = roster))
        val repository = repository(core, dispatcher)
        val seen = mutableListOf<TaffyReadiness>()
        val collector = launch(dispatcher) { repository.readiness.toList(seen) }
        advanceUntilIdle()

        core.publish(status(roster = roster, generation = 10uL))
        advanceUntilIdle()
        collector.cancel()
        assertEquals(listOf<TaffyReadiness>(Ready(direct)), seen)
    }

    private fun repository(
        core: CoreApiClient,
        dispatcher: CoroutineDispatcher,
        held: StateFlow<Set<String>> = MutableStateFlow(emptySet()),
        route: StateFlow<ProviderRoute> = MutableStateFlow(ProviderRoute.NOT_CONFIGURED),
    ) = CoreTaffyReadinessRepository(core, held, route, lifetime(dispatcher))
}

/** The recording double with a status that can be republished. */
private class PublishingCoreApiClient(initial: CoreStatus) : CoreApiClient by RecordingTaskCoreApiClient(initial) {
    private val statuses = MutableStateFlow(initial)
    override val status: StateFlow<CoreStatus> = statuses

    fun publish(status: CoreStatus) {
        statuses.value = status
    }
}

private fun lifetime(dispatcher: CoroutineDispatcher) = TaffyProfileLifetime(
    dispatchers = object : AppDispatchers {
        override val main = dispatcher
        override val default = dispatcher
        override val io = dispatcher
    },
    failureSink = CoroutineFailureSink { },
)

private fun credential(state: ProviderCredentialStateView) = StoredCredentialView(
    auth_method = ProviderAuthMethodView.API_KEY,
    state = state,
    subscription_backed = false,
    account_label = null,
    plan_label = null,
)

private fun entry(
    id: String,
    stored: StoredCredentialView? = null,
    enabled: Boolean = true,
    origin: ProviderOriginView = ProviderOriginView.CATALOG,
    layer: CatalogLayerView = CatalogLayerView.EMBEDDED_BASELINE,
) = ProviderRosterEntry(
    provider_id = id,
    display_name = id,
    origin = origin,
    auth_methods = listOf(ProviderAuthMethodView.API_KEY),
    stored = stored,
    signing_in = false,
    enabled = enabled,
    endpoint_host = null,
    configurable = true,
    endpoint_changed = false,
    catalog_layer = layer,
    selected_model_id = null,
    thinking = null,
    presentation = null,
    endpoint_base = null,
    last_refusal = null,
    model_count = 0u,
    refused_endpoint_host = null,
    subscription = false,
)

private fun status(
    roster: List<ProviderRosterEntry> = emptyList(),
    auth: AuthViewState? = null,
    generation: ULong = 9uL,
    projectionMode: CoreStatusProjectionMode = CoreStatusProjectionMode.COMPLETE,
) = CoreStatus(
    availability = CoreAvailability.READY,
    generation = generation,
    active_tasks = emptyList(),
    auth_state = auth,
    workspaces = emptyList(),
    workspace_export = null,
    asset_delivery = null,
    provider_roster = roster,
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
    projection_mode = projectionMode,
    projection_omissions = emptyList(),
)
