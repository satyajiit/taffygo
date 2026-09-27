// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.RosterStoredCredential
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-404's four categories, the tab it opens on, its three empty
 * answers and its two badges.
 */
class ProviderHubProjectionTest {

    // SCR-404's filing rule, at the level a person actually meets it. A vendor
    // offering both a plan and a key must be findable under Subscription, and
    // the entry there must offer the sign-in rather than the key form — the
    // whole point being that somebody who already pays opens that tab first.
    @Test
    fun `a vendor offering both a plan and a key is listed under both tabs`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(
                    row(
                        "xai",
                        methods = listOf(RosterAuthMethod.API_KEY, RosterAuthMethod.OAUTH),
                        subscription = true,
                    ),
                ),
            ),
            emptySet(),
            signInFlows = mapOf("xai" to true),
        )

        val byGroup = state.sections.associate { section -> section.group to section.rows }
        assertEquals(
            listOf(ProviderHubGroup.SUBSCRIPTION, ProviderHubGroup.BRING_YOUR_OWN_KEY),
            state.sections.map { it.group },
        )
        assertEquals(
            ProviderRowOffer.SignIn,
            byGroup.getValue(ProviderHubGroup.SUBSCRIPTION).single().offer,
        )
        assertEquals(
            ProviderRowOffer.Configure,
            byGroup.getValue(ProviderHubGroup.BRING_YOUR_OWN_KEY).single().offer,
        )
    }

    @Test
    fun `nothing is listed until the core has published a roster`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(ready = false, rows = listOf(row("anthropic"))),
            emptySet(),
        )

        // A core that has not spoken is "still coming"; rendering the rows it
        // carries anyway would make that state invisible.
        assertEquals(ProviderHubStatus.LOADING, state.status)
        assertEquals(emptyList<ProviderHubSection>(), state.sections)
    }

    @Test
    fun `a published roster with nothing in it is empty, not loading`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(ready = true),
            emptySet(),
        )

        assertEquals(ProviderHubStatus.EMPTY, state.status)
    }

    @Test
    fun `rows that can none of them be acted on are a failure of their own`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(row("a", enabled = false), row("b", configurable = false)),
            ),
            emptySet(),
        )

        // Still listed, because each row carries its own reason and two
        // different reasons have two different answers.
        assertEquals(ProviderHubStatus.BLOCKED, state.status)
        assertEquals(2, state.rows.size)
    }

    @Test
    fun `one actionable row is enough to make the screen ready`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(ready = true, rows = listOf(row("a", enabled = false), row("b"))),
            emptySet(),
        )

        assertEquals(ProviderHubStatus.READY, state.status)
    }

    @Test
    fun `the groups are drawn in order and only when they hold something`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(
                    row("keyed"),
                    row("mine", origin = RosterProviderOrigin.CUSTOM),
                    row("connected").copy(stored = credential()),
                ),
            ),
            emptySet(),
        )

        // Three rows in, two groups out: the connected one is not something to
        // add and is left to SCR-419.
        assertEquals(
            listOf(
                ProviderHubGroup.BRING_YOUR_OWN_KEY,
                ProviderHubGroup.YOUR_OWN_ENDPOINT,
            ),
            state.sections.map { it.group },
        )
        assertEquals(listOf(1, 1), state.sections.map { it.count })
    }

    // Verification report 2.14 read "Your own — 0 providers" straight after a
    // save that had worked, and took it for a save that had not. This is the
    // rule that produced it, stated as a fact rather than left to be
    // rediscovered on a phone: a provider carrying a key is set up, and SCR-404
    // is the screen a provider is *added* on.
    @Test
    fun `a saved own endpoint leaves the tab it was added from empty`() {
        val saved = row("bench", origin = RosterProviderOrigin.CUSTOM)
            .copy(stored = credential(), endpointBase = "http://192.168.1.39:8099/v1")

        val state = ProviderHubProjection.project(
            ProviderRosterState(ready = true, rows = listOf(saved)),
            setOf("bench"),
        )

        assertEquals(0, state.countIn(ProviderHubGroup.YOUR_OWN_ENDPOINT))
        assertEquals(ProviderHubStatus.ALL_CONNECTED, state.status)
        // And it is not missing. It is filed nowhere here for exactly the
        // reason `ConnectedProvidersProjection` keeps it: a credential stands
        // behind it, so it is managed rather than added.
        assertEquals(
            CredentialAvailability.PRESENT,
            ProviderRowDispatch.availabilityFor(saved, setOf("bench")),
        )
        assertEquals(ProviderRowOffer.EditEndpoint, ProviderRowDispatch.offerFor(saved, emptyMap()))
    }

    @Test
    fun `a tab counts its own section and a category with no section counts zero`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(row("keyed"), row("other"), row("connected").copy(stored = credential())),
            ),
            emptySet(),
        )

        // What a tab shows is the section's row list measured, so it cannot
        // disagree with the list the tab leads to.
        assertEquals(2, state.countIn(ProviderHubGroup.BRING_YOUR_OWN_KEY))
        assertEquals(
            listOf("keyed", "other"),
            state.rowsIn(ProviderHubGroup.BRING_YOUR_OWN_KEY).map { it.providerId },
        )
        // No section is built for a group that holds nothing. The tab is still
        // drawn, so it has to have an answer rather than an absence.
        assertEquals(0, state.countIn(ProviderHubGroup.SUBSCRIPTION))
        assertEquals(emptyList<ProviderHubRow>(), state.rowsIn(ProviderHubGroup.SUBSCRIPTION))
    }

    // A provider that is already working is on SCR-419, not here — and the
    // hub therefore reports its status against what is left to add. A phone
    // with everything set up is the opposite of a catalog this browser could
    // not read, and the two must not draw the same sentence.
    @Test
    fun `a roster whose every provider is connected says so rather than empty`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(
                    row("keyed").copy(stored = credential()),
                    row("other").copy(stored = credential()),
                ),
            ),
            emptySet(),
        )

        assertEquals(ProviderHubStatus.ALL_CONNECTED, state.status)
        assertEquals(emptyList<ProviderHubRow>(), state.rows)
    }

    @Test
    fun `an empty catalog is still told apart from everything being connected`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(ready = true),
            emptySet(),
        )

        assertEquals(ProviderHubStatus.EMPTY, state.status)
    }

    @Test
    fun `the screen opens on a tab that holds providers`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(row("mine", origin = RosterProviderOrigin.CUSTOM), row("keyed")),
            ),
            emptySet(),
        )

        // Group order decides, not roster order: keys come before a person's
        // own addresses, and Subscription is skipped because it holds nothing.
        assertEquals(ProviderHubGroup.BRING_YOUR_OWN_KEY, state.showing)
        assertTrue(state.rowsIn(state.showing).isNotEmpty())
    }

    @Test
    fun `a roster with nothing in it still names a category to be standing in`() {
        val loading = ProviderHubProjection.project(
            ProviderRosterState(ready = false),
            emptySet(),
        )
        val empty = ProviderHubProjection.project(
            ProviderRosterState(ready = true),
            emptySet(),
        )

        // Neither state draws a tab row — there is nothing to choose between —
        // so this is only about the screen never holding an absent category.
        assertEquals(ProviderHubGroup.SUBSCRIPTION, loading.showing)
        assertEquals(ProviderHubGroup.SUBSCRIPTION, empty.showing)
    }

    @Test
    fun `rows keep the order the core published them in, within their group`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(row("zeta"), row("alpha"), row("mid")),
            ),
            emptySet(),
        )

        assertEquals(
            listOf("zeta", "alpha", "mid"),
            state.sections.single().rows.map { it.providerId },
        )
    }

    // A sign-in in flight is the one credential-shaped fact a hub row can
    // carry, and it is why the row is still here: nothing has been stored yet.
    @Test
    fun `a sign-in in flight reaches the row`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(
                    row(
                        "vendor",
                        methods = listOf(RosterAuthMethod.OAUTH),
                        subscription = true,
                    ).copy(signingIn = true),
                ),
            ),
            emptySet(),
            signInFlows = mapOf("vendor" to true),
        )

        assertTrue(state.rows.single().signingIn)
    }

    // Two credentials used to draw two Connected badges here and no Default.
    // Neither badge exists on this screen any more, and the fact they stood
    // for is the same one: both providers are set up, so neither is on offer.
    @Test
    fun `two credentials leave the hub with nothing to offer`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(
                ready = true,
                rows = listOf(
                    row("one").copy(stored = credential()),
                    row("two").copy(stored = credential()),
                ),
            ),
            emptySet(),
        )

        assertEquals(emptyList<ProviderHubRow>(), state.rows)
        assertEquals(ProviderHubStatus.ALL_CONNECTED, state.status)
    }

    // The window between the browser holding a handle and the roster echoing
    // it must not re-offer the provider as something to set up: a person who
    // has just pasted a key would be invited to paste it again.
    @Test
    fun `a handle the roster has not echoed already takes the row off the hub`() {
        val state = ProviderHubProjection.project(
            ProviderRosterState(ready = true, rows = listOf(row("keyed"))),
            setOf("keyed"),
        )

        assertEquals(emptyList<ProviderHubRow>(), state.rows)
        assertEquals(ProviderHubStatus.ALL_CONNECTED, state.status)
    }

    private companion object {
        fun credential(
            state: RosterCredentialState = RosterCredentialState.USABLE,
            account: String? = null,
            plan: String? = null,
        ) = RosterStoredCredential(
            authMethod = RosterAuthMethod.API_KEY,
            state = state,
            subscriptionBacked = false,
            accountLabel = account,
            planLabel = plan,
        )

        fun row(
            providerId: String,
            origin: RosterProviderOrigin = RosterProviderOrigin.CATALOG,
            enabled: Boolean = true,
            methods: List<RosterAuthMethod> = listOf(RosterAuthMethod.API_KEY),
            subscription: Boolean = false,
            configurable: Boolean = true,
        ): ProviderRosterRow = ProviderRosterRow(
            providerId = providerId,
            displayName = providerId,
            origin = origin,
            authMethods = methods,
            stored = null,
            signingIn = false,
            enabled = enabled,
            configurable = configurable,
            subscription = subscription,
            catalogLayer = RosterCatalogLayer.EMBEDDED_BASELINE,
            selectedModelId = null,
            thinking = null,
            presentation = null,
            endpointBase = null,
            lastRefusal = null,
            modelCount = 0,
        )
    }
}
