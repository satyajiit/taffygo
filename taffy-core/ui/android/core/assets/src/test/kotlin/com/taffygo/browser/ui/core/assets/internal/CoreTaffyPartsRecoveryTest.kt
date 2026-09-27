// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets.internal

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.TaffyPartId
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.AssetDeliveryView
import taffy.core_api.AssetKindView
import taffy.core_api.AssetNetworkCostView
import taffy.core_api.AssetPresenceView
import taffy.core_api.AssetViewState
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

@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreTaffyPartsRecoveryTest {
    @Test
    fun `recovery projection hides parts and refuses request before dispatch`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingPartsCoreApiClient(recoveryStatus())
        val lifetime = lifetime(dispatcher)
        val repository = CoreTaffyPartsRepository(core, lifetime)
        advanceUntilIdle()

        assertFalse(repository.state.value.answered)
        assertTrue(repository.state.value.parts.isEmpty())
        assertEquals(
            FailureReason.CORE_UNAVAILABLE,
            repository.request(TaffyPartId("python-stdlib")).reasonOrNull(),
        )
        assertEquals(0, core.requestAssetCalls)
        lifetime.close()
    }
}

private fun recoveryStatus() = CoreStatus(
    availability = CoreAvailability.READY,
    generation = 4uL,
    active_tasks = emptyList(),
    auth_state = null,
    workspaces = emptyList(),
    workspace_export = null,
    asset_delivery = AssetDeliveryView(
        platform_supported = true,
        network_cost = AssetNetworkCostView.UNMETERED,
        metered_permitted = false,
        assets = listOf(
            AssetViewState(
                asset_id = "python-stdlib",
                asset_revision = "3.14.1-1",
                kind = AssetKindView.PYTHON_STDLIB,
                presence = AssetPresenceView.ABSENT,
                written_bytes = 0uL,
                total_bytes = 1uL,
                attempts = 0u,
                refusal = null,
                waiting_until_monotonic_ms = 0uL,
            ),
        ),
    ),
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
