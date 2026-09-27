// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CatalogLayerView
import taffy.core_api.ProviderOriginView
import taffy.core_api.ProviderRosterEntry

/** Screen SCR-418's pure half: what the page draws, and when it may be saved. */
class CustomEndpointProjectionTest {

    private val reachedWithModels = CustomEndpointOutcome.Reached(
        server = CustomEndpointOutcome.ServerKind.OPENAI_COMPATIBLE,
        modelCount = 4,
        models = listOf("a", "b", "c", "d").map(::fakeModel),
        provedBase = "http://192.168.1.9:11434/v1",
    )

    @Test
    fun `a roster that has not been published yet is loading, never gone`() {
        val state = CustomEndpointProjection.project(
            endpointId = "mine",
            roster = ProviderRosterState(),
            models = emptyMap(),
            draft = CustomEndpointDraft(),
        )

        assertEquals(CustomEndpointUiState.Status.LOADING, state.status)
    }

    @Test
    fun `a provider the published roster does not carry is gone rather than blank`() {
        val state = CustomEndpointProjection.project(
            endpointId = "mine",
            roster = ProviderRosterState(ready = true, rows = emptyList()),
            models = emptyMap(),
            draft = CustomEndpointDraft(),
        )

        assertEquals(CustomEndpointUiState.Status.UNKNOWN, state.status)
    }

    @Test
    fun `adding one needs no provider on the roster at all`() {
        val state = CustomEndpointProjection.project(
            endpointId = null,
            roster = ProviderRosterState(ready = true, rows = emptyList()),
            models = emptyMap(),
            draft = CustomEndpointDraft(),
        )

        assertEquals(CustomEndpointUiState.Status.READY, state.status)
        assertFalse(state.editing)
    }

    @Test
    fun `an empty field is not judged, and a typed one is`() {
        assertNull(project(CustomEndpointDraft()).refusal)
        assertEquals(
            CustomEndpointAddress.Refusal.CLEARTEXT_NOT_LOCAL,
            project(CustomEndpointDraft(address = "http://models.example.test/v1")).refusal,
        )
    }

    @Test
    fun `an unanswered proposal holds the save shut with nothing wrong`() {
        val state = project(
            CustomEndpointDraft(
                address = "http://192.168.1.9:11434",
                addressTouched = true,
                name = "Laptop",
                nameTouched = true,
                outcome = reachedWithModels,
                proposal = "http://192.168.1.9:11434/v1",
            ),
        )

        assertNull(state.refusal)
        assertTrue(state.proposalUnanswered)
        assertFalse(state.saveActionable)
    }

    @Test
    fun `answering the proposal either way lets the save through`() {
        val kept = project(
            CustomEndpointDraft(
                address = "http://192.168.1.9:11434",
                addressTouched = true,
                name = "Laptop",
                nameTouched = true,
                outcome = reachedWithModels,
                proposal = "http://192.168.1.9:11434/v1",
                keptAsTyped = true,
            ),
        )

        assertFalse(kept.proposalUnanswered)
        assertTrue(kept.saveActionable)
    }

    /**
     * Nobody is asked to name a model any more: the probe carries what it read,
     * so a server with models and a server with none are both saveable and the
     * write carries what the server actually said.
     */
    @Test
    fun `a reached address is saveable whether or not anything is loaded behind it`() {
        val named = CustomEndpointDraft(
            address = "http://192.168.1.9:11434/v1",
            addressTouched = true,
            name = "Laptop",
            nameTouched = true,
            outcome = reachedWithModels,
        )
        assertTrue(project(named).saveActionable)

        // Zero models is an answer, not a failure: the address is right and
        // nothing is loaded behind it, so the provider is saved with the empty
        // roster that is the truth about it.
        val empty = named.copy(
            outcome = CustomEndpointOutcome.Reached(
                server = CustomEndpointOutcome.ServerKind.OLLAMA,
                modelCount = 0,
                models = emptyList(),
                provedBase = null,
            ),
        )
        assertTrue(project(empty).saveActionable)
    }

    @Test
    fun `nothing is saved before the address has answered`() {
        val unprobed = CustomEndpointDraft(
            address = "http://192.168.1.9:11434/v1",
            addressTouched = true,
            name = "Laptop",
            nameTouched = true,
        )

        assertFalse(project(unprobed).saveActionable)
        assertTrue(project(unprobed).probeActionable)
    }

    @Test
    fun `the name shows the roster's until it is touched`() {
        val roster = ProviderRosterState(
            ready = true,
            rows = listOf(
                providerRow(
                    "mine",
                    displayName = "The laptop",
                    origin = RosterProviderOrigin.CUSTOM,
                    catalogLayer = RosterCatalogLayer.USER_OVERRIDE,
                    selectedModelId = "llama3.1:8b",
                ),
            ),
        )
        val models = mapOf("mine" to listOf(providerModel("mine", "llama3.1:8b", "Llama 3.1 8B")))

        val untouched = CustomEndpointProjection.project(
            endpointId = "mine",
            roster = roster,
            models = models,
            draft = CustomEndpointDraft(),
            currentHost = "192.168.1.9",
        )
        assertEquals("The laptop", untouched.name)
        assertEquals("Llama 3.1 8B", untouched.pinnedModelName)
        assertEquals("192.168.1.9", untouched.currentHost)

        val cleared = CustomEndpointProjection.project(
            endpointId = "mine",
            roster = roster,
            models = models,
            draft = CustomEndpointDraft(name = "", nameTouched = true),
        )
        assertEquals("", cleared.name)
    }

    @Test
    fun `an identity is folded out of the name and numbered on a collision`() {
        assertEquals("the-laptop", CustomEndpointProjection.mintProviderId("The laptop", setOf()))
        assertEquals(
            "the-laptop-2",
            CustomEndpointProjection.mintProviderId("The laptop!", setOf("the-laptop")),
        )
        assertEquals(
            "the-laptop-3",
            CustomEndpointProjection.mintProviderId(
                "The laptop",
                setOf("the-laptop", "the-laptop-2"),
            ),
        )
        assertEquals("endpoint", CustomEndpointProjection.mintProviderId("!!!", setOf()))
        // The alphabet the browser's provider store files a record under, and
        // the first byte may never be the separator.
        val minted = CustomEndpointProjection.mintProviderId("  ---Ollama  ", setOf())
        assertTrue(minted, minted.matches(Regex("[a-z0-9][a-z0-9-]{0,63}")))
    }

    @Test
    fun `a probe that reached nothing proposes nothing`() {
        assertNull(
            CustomEndpointProjection.proposalFor(
                "http://192.168.1.9:11434",
                CustomEndpointOutcome.Refused(CustomEndpointOutcome.Problem.NOTHING_ANSWERED),
            ),
        )
        assertNull(CustomEndpointProjection.proposalFor("http://192.168.1.9:11434", null))
    }

    /**
     * The proposal is the base the prober demonstrated, not one worked out
     * here. That is the whole difference: the prober knows which of its steps
     * answered, and this screen would only have been guessing from a port.
     */
    @Test
    fun `a base proved somewhere other than the typed address is proposed`() {
        assertEquals(
            "http://192.168.1.9:11434/v1",
            CustomEndpointProjection.proposalFor("http://192.168.1.9:11434", reachedWithModels),
        )
        // A trailing separator is not a different address, and offering one
        // would ask a person to settle a question nobody has.
        assertNull(
            CustomEndpointProjection.proposalFor(
                "http://192.168.1.9:11434/v1/",
                reachedWithModels,
            ),
        )
    }

    /**
     * Absent means nothing was proved, which is said by proposing nothing —
     * never by offering a value nobody demonstrated.
     */
    @Test
    fun `a probe that proved no base proposes nothing`() {
        assertNull(
            CustomEndpointProjection.proposalFor(
                "http://192.168.1.9:11434",
                reachedWithModels.copy(provedBase = null),
            ),
        )
    }

    /**
     * Editing starts at the address the core registered, and stops the moment
     * a person touches the field — otherwise the next published snapshot puts
     * the saved address back under what they are typing.
     */
    @Test
    fun `the address shows what the core registered until it is touched`() {
        val roster = ProviderRosterState(
            ready = true,
            rows = listOf(
                providerRow(
                    "mine",
                    origin = RosterProviderOrigin.CUSTOM,
                    catalogLayer = RosterCatalogLayer.USER_OVERRIDE,
                ),
            ),
        )

        val untouched = CustomEndpointProjection.project(
            endpointId = "mine",
            roster = roster,
            models = emptyMap(),
            draft = CustomEndpointDraft(),
            currentHost = "192.168.1.9",
            currentAddress = "http://192.168.1.9:11434/v1",
        )
        assertEquals("http://192.168.1.9:11434/v1", untouched.address)
        assertNull(untouched.refusal)

        val cleared = CustomEndpointProjection.project(
            endpointId = "mine",
            roster = roster,
            models = emptyMap(),
            draft = CustomEndpointDraft(address = "", addressTouched = true),
            currentAddress = "http://192.168.1.9:11434/v1",
        )
        assertEquals("", cleared.address)
    }

    @Test
    fun `endpoint flows ignore unrelated roster facts and invalidate only their own value`() {
        val baseline = listOf(endpointEntry("host.test", "http://host.test:11434/v1"))
        val renamed = baseline.map { it.copy(display_name = "Renamed") }
        assertTrue(sameProviderHostProjection(baseline, renamed))
        assertTrue(sameProviderAddressProjection(baseline, renamed))

        val hostChanged = baseline.map { it.copy(endpoint_host = "other.test") }
        assertFalse(sameProviderHostProjection(baseline, hostChanged))
        assertTrue(sameProviderAddressProjection(baseline, hostChanged))

        val addressChanged = baseline.map { it.copy(endpoint_base = "http://host.test:8080") }
        assertTrue(sameProviderHostProjection(baseline, addressChanged))
        assertFalse(sameProviderAddressProjection(baseline, addressChanged))
    }

    private fun project(draft: CustomEndpointDraft): CustomEndpointUiState =
        CustomEndpointProjection.project(
            endpointId = null,
            roster = ProviderRosterState(ready = true, rows = emptyList()),
            models = emptyMap(),
            draft = draft,
        )

    private fun endpointEntry(host: String, address: String) = ProviderRosterEntry(
        provider_id = "mine",
        display_name = "Laptop",
        origin = ProviderOriginView.CUSTOM,
        auth_methods = emptyList(),
        stored = null,
        signing_in = false,
        enabled = true,
        endpoint_host = host,
        configurable = true,
        endpoint_changed = false,
        catalog_layer = CatalogLayerView.USER_OVERRIDE,
        selected_model_id = null,
        thinking = null,
        presentation = null,
        endpoint_base = address,
        last_refusal = null,
        model_count = 0u,
        refused_endpoint_host = null,
        subscription = false,
    )
}
