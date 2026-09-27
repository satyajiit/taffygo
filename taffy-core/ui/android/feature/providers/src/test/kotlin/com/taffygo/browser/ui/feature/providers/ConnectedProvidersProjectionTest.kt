// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.RosterProviderRefusal
import com.taffygo.browser.ui.core.model.ThinkingLevel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-419's pure half: the shorter list, and what each row is set to. */
class ConnectedProvidersProjectionTest {

    @Test
    fun `a roster that has not been published yet is loading, never empty`() {
        val state = ConnectedProvidersProjection.project(
            roster = ProviderRosterState(),
            models = emptyMap(),
            browserHeldCredentialIds = emptySet(),
        )

        assertEquals(ConnectedProvidersUiState.Status.LOADING, state.status)
        assertTrue(state.rows.isEmpty())
    }

    @Test
    fun `only what is connected is listed`() {
        val state = project(
            rows = listOf(
                providerRow("keyed", stored = storedCredential()),
                providerRow("offered"),
            ),
        )

        assertEquals(ConnectedProvidersUiState.Status.READY, state.status)
        assertEquals(listOf("keyed"), state.rows.map { it.providerId })
    }

    @Test
    fun `a saved keyless endpoint is connected and its models can be chosen`() {
        val base = providerRow("mine", endpointBase = "http://localhost:8765/v1", modelCount = 1)
        listOf(
            base.copy(origin = RosterProviderOrigin.CUSTOM),
            base.copy(catalogLayer = RosterCatalogLayer.USER_OVERRIDE),
        ).forEach { saved ->
            val roster = ProviderRosterState(ready = true, rows = listOf(saved))
            val models = mapOf("mine" to listOf(providerModel("mine", "one")))
            val connected = project(roster.rows, models)

            assertFalse(connected.handsOverToTheHub)
            val row = connected.rows.single()
            assertEquals(CredentialAvailability.ABSENT, row.availability)
            assertTrue(row.ownEndpoint)
            assertFalse(row.canSignOut)
            assertNull(project(roster.rows, confirming = "mine").confirming)
            assertEquals(ProviderRowOffer.EditEndpoint, row.offer)
            assertTrue(row.carriesStandingChoice)
            assertTrue(ProviderHubProjection.project(roster, emptySet()).rows.isEmpty())

            val picker = ModelSelectionProjection.project(
                "mine", roster, models, emptySet(), ModelSelectionDraft(),
            )
            assertFalse(picker.rows.single().locked)
            assertTrue(picker.locked.isEmpty())
        }
    }

    @Test
    fun `an own row without a registered address and a catalog address stay unconnected`() {
        val own = providerRow("mine", origin = RosterProviderOrigin.CUSTOM)
        listOf(
            own,
            own.copy(endpointBase = ""),
            own.copy(endpointBase = "  "),
            providerRow("mine", endpointBase = "https://provider.example/v1"),
        ).forEach { unsaved ->
            val roster = ProviderRosterState(ready = true, rows = listOf(unsaved))
            assertTrue(project(roster.rows).handsOverToTheHub)
            assertEquals(
                "mine", ProviderHubProjection.project(roster, emptySet()).rows.single().providerId,
            )
            val picker = ModelSelectionProjection.project(
                "mine", roster, mapOf("mine" to listOf(providerModel("mine", "one"))),
                emptySet(), ModelSelectionDraft(),
            )
            assertTrue(picker.rows.single().locked)
        }
    }

    @Test
    fun `a keyless endpoint makes a usable vendor choice shared but not a failed credential usable`() {
        val own = providerRow(
            "mine", origin = RosterProviderOrigin.CUSTOM, endpointBase = "http://localhost:8765/v1",
            modelCount = 1,
        )
        val vendor = providerRow("keyed", stored = storedCredential())
        val rows = listOf(own, vendor)
        assertEquals(
            listOf(false, false), project(rows).rows.map { it.carriesStandingChoice },
        )
        val configured = ProviderConfigProjection.project(
            "keyed", ProviderRosterState(ready = true, rows = rows), emptyMap(), emptySet(),
            ProviderConfigDraft(),
        )
        assertEquals(ProviderDefaultChoice.SHARED, configured.defaultChoice)
        val failed = own.copy(stored = storedCredential(state = RosterCredentialState.NEEDS_SIGN_IN))
        val unknown = project(listOf(failed)).rows.single()
        assertEquals(CredentialAvailability.UNKNOWN, unknown.availability)
        assertTrue(unknown.canSignOut)
        assertFalse(unknown.carriesStandingChoice)
        val heldOnly = project(listOf(own), heldCredentialIds = setOf("mine")).rows.single()
        assertEquals(CredentialAvailability.UNKNOWN, heldOnly.availability)
        assertFalse(heldOnly.canSignOut)
        assertFalse(heldOnly.carriesStandingChoice)
    }

    @Test
    fun `a keyless address does not make an unpublished roster ready`() {
        val roster = ProviderRosterState(
            ready = false,
            rows = listOf(providerRow(
                "mine", origin = RosterProviderOrigin.CUSTOM, endpointBase = "http://localhost:8765/v1",
            )),
        )
        val models = mapOf("mine" to listOf(providerModel("mine", "one")))
        val connected = ConnectedProvidersProjection.project(
            roster, models, emptySet(),
        )
        assertEquals(ConnectedProvidersUiState.Status.LOADING, connected.status)
        assertTrue(connected.rows.isEmpty())
        assertFalse(connected.handsOverToTheHub)
        assertEquals(ProviderHubStatus.LOADING, ProviderHubProjection.project(roster, emptySet()).status)
        val picker = ModelSelectionProjection.project(
            "mine", roster, models, emptySet(), ModelSelectionDraft(),
        )
        assertEquals(ModelSelectionUiState.Status.LOADING, picker.status)
        assertTrue(picker.rows.isEmpty())
    }

    @Test
    fun `a saved endpoint with no routable models stays configured without a standing choice`() {
        val own = providerRow(
            "mine", origin = RosterProviderOrigin.CUSTOM, endpointBase = "http://localhost:8765/v1",
            modelCount = 0,
        )
        val alone = project(listOf(own))
        assertFalse(alone.handsOverToTheHub)
        assertFalse(alone.rows.single().carriesStandingChoice)
        val rows = listOf(own, providerRow("keyed", stored = storedCredential()))
        assertEquals(
            listOf(false, true), project(rows).rows.map { it.carriesStandingChoice },
        )
        val configured = ProviderConfigProjection.project(
            "keyed", ProviderRosterState(ready = true, rows = rows), emptyMap(), emptySet(),
            ProviderConfigDraft(),
        )
        assertEquals(ProviderDefaultChoice.IN_FORCE, configured.defaultChoice)
    }

    @Test
    fun `a roster with nothing connected is empty rather than missing`() {
        val state = project(rows = listOf(providerRow("offered")))

        assertEquals(ConnectedProvidersUiState.Status.EMPTY, state.status)
    }

    // Where the providers area is entered decides which of the two screens a
    // person lands on, and the rule is this one property. It must not fire on
    // a roster that has not arrived: forwarding somebody off this screen on
    // the strength of a list the core has not published yet would send every
    // launch to the hub, connected providers or not.
    @Test
    fun `only a published roster with nothing on it hands over to the hub`() {
        val loading = ConnectedProvidersProjection.project(
            roster = ProviderRosterState(ready = false),
            models = emptyMap(),
            browserHeldCredentialIds = emptySet(),
        )
        val nothingConnected = project(rows = listOf(providerRow("offered")))
        val somethingConnected = project(
            rows = listOf(providerRow("keyed", stored = storedCredential())),
        )

        assertEquals(
            listOf(false, true, false),
            listOf(
                loading.handsOverToTheHub,
                nothingConnected.handsOverToTheHub,
                somethingConnected.handsOverToTheHub,
            ),
        )
    }

    @Test
    fun `a handle the core has not echoed is listed and offers nothing to remove`() {
        val state = project(
            rows = listOf(providerRow("keyed")),
            heldCredentialIds = setOf("keyed"),
        )

        val row = state.rows.single()
        assertEquals(CredentialAvailability.UNKNOWN, row.availability)
        assertFalse(row.canSignOut)
    }

    @Test
    fun `each row says what a request sent to it would be`() {
        val state = project(
            rows = listOf(
                providerRow(
                    "keyed",
                    stored = storedCredential(),
                    selectedModelId = "m-1",
                    thinking = ThinkingLevel.MEDIUM,
                ),
            ),
            models = mapOf("keyed" to listOf(providerModel("keyed", "m-1", "A model"))),
        )

        val row = state.rows.single()
        assertEquals("A model", row.modelName)
        assertEquals(ThinkingLevel.MEDIUM, row.thinking)
        assertTrue(row.canSignOut)
    }

    // Registry document section 5.4: a refusal reaches a surface. It rides
    // beside the row's state rather than replacing it, because a rate limit
    // is a fact about a request and the credential behind it still works.
    @Test
    fun `the vendor's last refusal is carried whole and absent when there is none`() {
        val refused = refusal(RosterProviderRefusal.BILLING, atMonotonicMs = 9uL)
        val state = project(
            rows = listOf(
                providerRow("refused", stored = storedCredential(), lastRefusal = refused),
                providerRow("fine", stored = storedCredential()),
            ),
        )

        val (refusedRow, fineRow) = state.rows
        assertEquals(refused, refusedRow.lastRefusal)
        assertEquals(CredentialAvailability.PRESENT, refusedRow.availability)
        assertTrue(refusedRow.canSignOut)
        assertNull(fineRow.lastRefusal)
    }

    @Test
    fun `a pin the roster no longer carries shows as no pin`() {
        val state = project(
            rows = listOf(
                providerRow("keyed", stored = storedCredential(), selectedModelId = "gone"),
            ),
            models = mapOf("keyed" to listOf(providerModel("keyed", "m-1"))),
        )

        assertNull(state.rows.single().modelName)
    }

    @Test
    fun `one usable credential carries the standing choice and two carry neither`() {
        val one = project(
            rows = listOf(
                providerRow("keyed", stored = storedCredential()),
                providerRow("half", stored = storedCredential(RosterAuthMethod.API_KEY,
                    RosterCredentialState.NEEDS_SIGN_IN)),
            ),
        )
        assertEquals(listOf(true, false), one.rows.map { it.carriesStandingChoice })

        val two = project(
            rows = listOf(
                providerRow("keyed", stored = storedCredential()),
                providerRow("other", stored = storedCredential()),
            ),
        )
        assertEquals(listOf(false, false), two.rows.map { it.carriesStandingChoice })
    }

    @Test
    fun `a provider the person defined is marked as their own`() {
        val state = project(
            rows = listOf(
                providerRow(
                    "mine",
                    stored = storedCredential(),
                    origin = RosterProviderOrigin.CUSTOM,
                    catalogLayer = RosterCatalogLayer.USER_OVERRIDE,
                ),
            ),
        )

        val row = state.rows.single()
        assertTrue(row.ownEndpoint)
        assertEquals(ProviderRowOffer.EditEndpoint, row.offer)
    }

    @Test
    fun `a confirmation about a provider that has left the roster closes with it`() {
        val open = project(
            rows = listOf(providerRow("keyed", stored = storedCredential())),
            confirming = "keyed",
        )
        assertEquals("keyed", open.confirming?.providerId)

        val gone = project(
            rows = listOf(providerRow("keyed", stored = storedCredential())),
            confirming = "someone-else",
        )
        assertNull(gone.confirming)
    }

    private fun project(
        rows: List<com.taffygo.browser.ui.core.model.ProviderRosterRow>,
        models: Map<String, List<com.taffygo.browser.ui.core.model.ProviderModel>> = emptyMap(),
        heldCredentialIds: Set<String> = emptySet(),
        confirming: String? = null,
    ): ConnectedProvidersUiState = ConnectedProvidersProjection.project(
        roster = ProviderRosterState(ready = true, rows = rows),
        models = models,
        browserHeldCredentialIds = heldCredentialIds,
        confirmingProviderId = confirming,
    )
}
