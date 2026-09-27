// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
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
import taffy.core_api.ProviderRosterEntry
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedDetailsView
import taffy.core_api.SavedSignInsView

internal class CoreCustomEndpointsRecoveryTest {
    @Test
    fun `recovery projection hides endpoints and refuses every mutation before dispatch`() =
        runTest {
            val core = RecordingCustomEndpointCoreApiClient(recoveryStatus())
            val endpoints = CoreCustomEndpoints(core, verdictWaitMillis = 1L)

            assertTrue(endpoints.hosts.first().isEmpty())
            assertTrue(endpoints.addresses.first().isEmpty())
            assertEquals(
                CustomEndpointOutcome.Refused(CustomEndpointOutcome.Problem.NO_ANSWER),
                endpoints.probe(
                    address = "https://provider.example.test/v1",
                    providerId = "provider",
                    credentialHandle = null,
                ),
            )
            assertTrue(
                runCatching {
                    endpoints.save(
                        providerId = "provider",
                        displayName = "Provider",
                        address = "https://provider.example.test/v1",
                        models = emptyList(),
                        server = null,
                        credentialHandle = null,
                    )
                }.exceptionOrNull() is IllegalStateException,
            )
            assertTrue(
                runCatching { endpoints.remove("provider") }.exceptionOrNull()
                    is IllegalStateException,
            )

            assertEquals(0, core.probeCalls)
            assertEquals(0, core.saveCalls)
            assertEquals(0, core.removeCalls)
        }
}

private fun recoveryStatus() = CoreStatus(
    availability = CoreAvailability.READY,
    generation = 1uL,
    active_tasks = emptyList(),
    auth_state = null,
    workspaces = emptyList(),
    workspace_export = null,
    asset_delivery = null,
    provider_roster = listOf(
        ProviderRosterEntry(
            provider_id = "provider",
            display_name = "Provider",
            origin = ProviderOriginView.CUSTOM,
            auth_methods = listOf(ProviderAuthMethodView.API_KEY),
            stored = null,
            signing_in = false,
            enabled = true,
            endpoint_host = "provider.example.test",
            configurable = true,
            endpoint_changed = false,
            catalog_layer = CatalogLayerView.USER_OVERRIDE,
            selected_model_id = null,
            thinking = null,
            presentation = null,
            endpoint_base = "https://provider.example.test/v1",
            last_refusal = null,
            model_count = 0u,
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
    projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
    projection_omissions = emptyList(),
)
