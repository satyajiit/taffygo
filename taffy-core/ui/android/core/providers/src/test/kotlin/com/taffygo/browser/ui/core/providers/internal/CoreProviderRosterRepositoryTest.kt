// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providers.internal

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.Clock
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.RosterProviderRefusal
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import taffy.core_api.AssistantConfigurationView
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
import taffy.core_api.ProviderOriginView
import taffy.core_api.ProviderRefusalStateView
import taffy.core_api.ProviderRefusalView
import taffy.core_api.ProviderRosterEntry
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedDetailsView
import taffy.core_api.SavedSignInsView

/**
 * The one reading this repository adds to the core's roster: when this
 * profile first saw a refusal, as a wall-clock moment a surface can show.
 *
 * The core's `at_monotonic_ms` is exact and cannot be rendered as a time, so
 * the stamp is taken here — once per distinct monotonic reading, so a roster
 * republished for another reason does not make an old refusal read as new,
 * and forgotten when the refusal clears, so the next one is not dated from
 * the last.
 */
@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreProviderRosterRepositoryTest {

    private val clock = SteppingClock()

    @Test
    fun `a refusal is stamped when first seen and the stamp survives a republish`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingRosterCoreApiClient(status(refusedAt = 5uL))
        clock.now = 1_000L
        val repository = CoreProviderRosterRepository(core, lifetime(dispatcher), clock)
        runCurrent()

        val first = repository.roster.value.rows.single().lastRefusal
        assertEquals(RosterProviderRefusal.RATE_LIMIT, first?.refusal)
        assertEquals(5uL, first?.atMonotonicMs)
        assertEquals(1_000L, first?.observedAtEpochMillis)

        // The same refusal, republished because something else moved: the
        // stamp is the first sighting, not the latest snapshot.
        clock.now = 9_000L
        core.publish(status(refusedAt = 5uL, generation = 2uL, selected = "other"))
        runCurrent()
        assertEquals(1_000L, repository.roster.value.rows.single().lastRefusal?.observedAtEpochMillis)
    }

    @Test
    fun `a new reading is a new refusal and is stamped afresh`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingRosterCoreApiClient(status(refusedAt = 5uL))
        clock.now = 1_000L
        val repository = CoreProviderRosterRepository(core, lifetime(dispatcher), clock)
        runCurrent()

        clock.now = 4_000L
        core.publish(status(refusedAt = 77uL, refusal = ProviderRefusalView.OVERLOADED))
        runCurrent()

        val refreshed = repository.roster.value.rows.single().lastRefusal
        assertEquals(RosterProviderRefusal.OVERLOADED, refreshed?.refusal)
        assertEquals(4_000L, refreshed?.observedAtEpochMillis)
    }

    @Test
    fun `a refusal that clears forgets its stamp so the next one is dated from its own sighting`() =
        runTest {
            val dispatcher = StandardTestDispatcher(testScheduler)
            val core = RecordingRosterCoreApiClient(status(refusedAt = 5uL))
            clock.now = 1_000L
            val repository = CoreProviderRosterRepository(core, lifetime(dispatcher), clock)
            runCurrent()

            core.publish(status(refusedAt = null))
            runCurrent()
            assertNull(repository.roster.value.rows.single().lastRefusal)

            // The same monotonic reading coming back is a new sighting: the
            // earlier stamp went with the refusal it belonged to.
            clock.now = 8_000L
            core.publish(status(refusedAt = 5uL))
            runCurrent()
            assertEquals(8_000L, repository.roster.value.rows.single().lastRefusal?.observedAtEpochMillis)
        }

    @Test
    fun `a row that refuses nothing is published without a stamp`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingRosterCoreApiClient(status(refusedAt = null))
        val repository = CoreProviderRosterRepository(core, lifetime(dispatcher), clock)
        runCurrent()

        assertNull(repository.roster.value.rows.single().lastRefusal)
        assertEquals(0, clock.reads)
    }

    private class SteppingClock : Clock {
        var now: Long = 0L
        var reads: Int = 0

        override fun nowEpochMillis(): Long {
            reads += 1
            return now
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

    private fun status(
        refusedAt: ULong?,
        refusal: ProviderRefusalView = ProviderRefusalView.RATE_LIMIT,
        generation: ULong = 1uL,
        selected: String? = null,
    ) = CoreStatus(
        availability = CoreAvailability.READY,
        generation = generation,
        active_tasks = emptyList(),
        auth_state = null,
        workspaces = emptyList(),
        workspace_export = null,
        asset_delivery = null,
        provider_roster = listOf(
            ProviderRosterEntry(
                provider_id = "provider",
                display_name = "Provider",
                origin = ProviderOriginView.CATALOG,
                auth_methods = listOf(ProviderAuthMethodView.API_KEY),
                stored = null,
                signing_in = false,
                enabled = true,
                endpoint_host = "provider.example.test",
                configurable = true,
                endpoint_changed = false,
                catalog_layer = CatalogLayerView.EMBEDDED_BASELINE,
                selected_model_id = selected,
                thinking = null,
                presentation = null,
                endpoint_base = null,
                last_refusal = refusedAt?.let {
                    ProviderRefusalStateView(refusal = refusal, at_monotonic_ms = it)
                },
                model_count = 1u,
                refused_endpoint_host = null,
                subscription = false,
            ),
        ),
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
}
