// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.RosterStoredCredential
import com.taffygo.browser.ui.core.ui.TaffyDestination
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** The one rule screen SCR-404 is built on, driven fact by fact. */
class ProviderRowDispatchTest {

    @Test
    fun `a key row goes to the provider's own page`() {
        val offer = ProviderRowDispatch.offerFor(row("openrouter"), NO_FLOWS)

        assertEquals(ProviderRowOffer.Configure, offer)
        assertEquals(
            TaffyDestination.ProviderConfig("openrouter"),
            ProviderRowDispatch.destinationFor(hubRow("openrouter", offer)),
        )
    }

    @Test
    fun `a plan row this build can run goes to that vendor's sign-in`() {
        val offer = ProviderRowDispatch.offerFor(
            row("a-vendor", methods = listOf(RosterAuthMethod.OAUTH), subscription = true),
            ALL_FLOWS,
        )

        assertEquals(ProviderRowOffer.SignIn, offer)
        assertEquals(
            TaffyDestination.ProviderSignIn("a-vendor"),
            ProviderRowDispatch.destinationFor(hubRow("a-vendor", offer)),
        )
    }

    @Test
    fun `a plan row this build cannot run is explained, never turned into a key form`() {
        val offer = ProviderRowDispatch.offerFor(
            row("a-vendor", methods = listOf(RosterAuthMethod.OAUTH), subscription = true),
            NO_FLOWS,
        )

        // The sign-in names pinned origins a served catalog cannot invent, so
        // the row is available and not startable. Falling back to Configure
        // would open a key form for a provider that takes no key.
        assertEquals(
            ProviderRowOffer.Blocked(ProviderRowOffer.Reason.SIGN_IN_NOT_BUILT),
            offer,
        )
        assertNull(ProviderRowDispatch.destinationFor(hubRow("a-vendor", offer)))
    }

    @Test
    fun `a person's own provider is edited where it was typed`() {
        val byOrigin = row("mine", origin = RosterProviderOrigin.CUSTOM)
        val byLayer = row("also-mine", catalogLayer = RosterCatalogLayer.USER_OVERRIDE)

        // Two independent fields say "the person supplied this" and either is
        // enough: misreading one sends somebody to a key form for an address
        // they typed.
        assertEquals(ProviderRowOffer.EditEndpoint, ProviderRowDispatch.offerFor(byOrigin, NO_FLOWS))
        assertEquals(ProviderRowOffer.EditEndpoint, ProviderRowDispatch.offerFor(byLayer, NO_FLOWS))
        assertEquals(
            TaffyDestination.CustomEndpointSetup("mine"),
            ProviderRowDispatch.destinationFor(
                hubRow("mine", ProviderRowOffer.EditEndpoint),
            ),
        )
    }

    @Test
    fun `a refusal outranks every method the row offers`() {
        val unconfigurable = row("x", configurable = false)
        val shut = row("x", enabled = false)

        assertEquals(
            listOf(
                ProviderRowOffer.Reason.NOT_ACTIONABLE,
                ProviderRowOffer.Reason.HELD_SHUT,
            ),
            listOf(unconfigurable, shut).map {
                (ProviderRowDispatch.offerFor(it, ALL_FLOWS) as ProviderRowOffer.Blocked).reason
            },
        )
    }

    @Test
    fun `a stored credential is managed on the provider's own page whatever saved it`() {
        val signedIn = row("a-vendor", methods = listOf(RosterAuthMethod.OAUTH), subscription = true)
            .copy(stored = credential(subscriptionBacked = true))

        assertEquals(ProviderRowOffer.Configure, ProviderRowDispatch.offerFor(signedIn, ALL_FLOWS))
    }

    @Test
    fun `a row naming no method says so rather than guessing one`() {
        val offer = ProviderRowDispatch.offerFor(row("x", methods = emptyList()), ALL_FLOWS)

        assertEquals(ProviderRowOffer.Blocked(ProviderRowOffer.Reason.NO_METHOD), offer)
    }

    @Test
    fun `a usable credential is present and nothing stored anywhere is absent`() {
        assertEquals(
            CredentialAvailability.PRESENT,
            ProviderRowDispatch.availabilityFor(
                row("x").copy(stored = credential()),
                emptySet(),
            ),
        )
        assertEquals(
            CredentialAvailability.ABSENT,
            ProviderRowDispatch.availabilityFor(row("x"), emptySet()),
        )
    }

    @Test
    fun `a transient failure leaves a working row unknown rather than locked`() {
        val refreshFailed = row("x")
            .copy(stored = credential(state = RosterCredentialState.REFRESH_FAILED))
        val staleSignIn = row("x")
            .copy(stored = credential(state = RosterCredentialState.NEEDS_SIGN_IN))
        val notEchoedYet = row("x")

        // None of the three may read as "no credential": the first two hold
        // one the vendor has not confirmed, and the third is a handle the
        // browser holds that the roster has not echoed back yet.
        assertEquals(
            listOf(
                CredentialAvailability.UNKNOWN,
                CredentialAvailability.UNKNOWN,
                CredentialAvailability.UNKNOWN,
            ),
            listOf(
                ProviderRowDispatch.availabilityFor(refreshFailed, emptySet()),
                ProviderRowDispatch.availabilityFor(staleSignIn, emptySet()),
                ProviderRowDispatch.availabilityFor(notEchoedYet, setOf("x")),
            ),
        )
    }

    // The hub is the screen a provider is added on, so one that is already
    // working is not on it at all — whichever way the credential arrived, and
    // whether or not the roster has echoed it back yet. SCR-419 picks up
    // exactly the rows this drops.
    @Test
    fun `a credential takes a row off the hub whatever way it came in`() {
        val plan = row("a-vendor", methods = listOf(RosterAuthMethod.OAUTH), subscription = true)
        val own = row("mine", origin = RosterProviderOrigin.CUSTOM)

        assertEquals(
            listOf(emptyList<ProviderHubGroup>(), emptyList()),
            listOf(
                ProviderRowDispatch.groupsFor(plan, CredentialAvailability.PRESENT),
                ProviderRowDispatch.groupsFor(own, CredentialAvailability.UNKNOWN),
            ),
        )
    }

    @Test
    fun `an unconnected row is filed by what it would take`() {
        assertEquals(
            listOf(
                listOf(ProviderHubGroup.YOUR_OWN_ENDPOINT),
                listOf(ProviderHubGroup.SUBSCRIPTION),
                listOf(ProviderHubGroup.BRING_YOUR_OWN_KEY),
                listOf(ProviderHubGroup.BRING_YOUR_OWN_KEY),
            ),
            listOf(
                row("mine", origin = RosterProviderOrigin.CUSTOM),
                row("a-vendor", methods = listOf(RosterAuthMethod.OAUTH), subscription = true),
                row("keyed"),
                row("no-method", methods = emptyList()),
            ).map { ProviderRowDispatch.groupsFor(it, CredentialAvailability.ABSENT) },
        )
    }

    // One connected provider is unambiguous, so the product names it. This
    // lived on the hub's own projection until the hub stopped listing
    // connected providers at all; the rule is the same one and SCR-419 and a
    // provider's own page both ask it here. It used to take a route as well,
    // because the managed route sent every request past every provider in the
    // catalog and so carried none of them; with that route gone the rule
    // reads nothing but the roster.
    @Test
    fun `one connected provider is where a request would go`() {
        val onlyOne = mapOf("keyed" to CredentialAvailability.PRESENT)
        val rows = listOf(row("keyed"))

        assertEquals("keyed", ProviderRowDispatch.standingChoice(onlyOne, rows))
    }

    // The defect this rule exists for: a vendor offering both a plan and a key
    // was filed with the keys alone, so somebody who pays for the plan opened
    // Subscription and did not find it — while the sign-in sat in the binary,
    // dated and working. Both tabs, and each entry answers its own question.
    @Test
    fun `a row offering both a plan and a key is filed under both`() {
        val both = row(
            "both",
            methods = listOf(RosterAuthMethod.API_KEY, RosterAuthMethod.OAUTH),
            subscription = true,
        )

        assertEquals(
            listOf(ProviderHubGroup.SUBSCRIPTION, ProviderHubGroup.BRING_YOUR_OWN_KEY),
            ProviderRowDispatch.groupsFor(both, CredentialAvailability.ABSENT),
        )
    }

    @Test
    fun `each group asks its own question of the same row`() {
        val both = row(
            "both",
            methods = listOf(RosterAuthMethod.API_KEY, RosterAuthMethod.OAUTH),
            subscription = true,
        )
        val vendors = mapOf("both" to true)

        assertEquals(
            ProviderRowOffer.SignIn,
            ProviderRowDispatch.offerFor(both, vendors, ProviderHubGroup.SUBSCRIPTION),
        )
        assertEquals(
            ProviderRowOffer.Configure,
            ProviderRowDispatch.offerFor(both, vendors, ProviderHubGroup.BRING_YOUR_OWN_KEY),
        )
        // With no group named, the primary way in is unchanged: the key form,
        // which is what a picker and the connected list have always shown.
        assertEquals(ProviderRowOffer.Configure, ProviderRowDispatch.offerFor(both, vendors))
    }

    @Test
    fun `the subscription entry says so when this build has no flow for it`() {
        val both = row(
            "both",
            methods = listOf(RosterAuthMethod.API_KEY, RosterAuthMethod.OAUTH),
            subscription = true,
        )

        assertEquals(
            ProviderRowOffer.Blocked(ProviderRowOffer.Reason.SIGN_IN_NOT_BUILT),
            ProviderRowDispatch.offerFor(both, NO_FLOWS, ProviderHubGroup.SUBSCRIPTION),
        )
    }

    // The defect this arm exists for: every vendor this binary compiles a flow
    // for had no dated terms review until decision 0113 dated the rows, so
    // the browser refused them at the first leg. They were offered Sign in
    // anyway, and the press answered "not available on this device" — a
    // sentence about the phone, for a state that is about neither the phone
    // nor the person.
    @Test
    fun `a flow the browser will not start is said apart from one that is missing`() {
        val vendor = row(
            "a-vendor",
            methods = listOf(RosterAuthMethod.OAUTH),
            subscription = true,
        )

        assertEquals(
            listOf(
                ProviderRowOffer.Blocked(ProviderRowOffer.Reason.SIGN_IN_NOT_BUILT),
                ProviderRowOffer.Blocked(ProviderRowOffer.Reason.SIGN_IN_NOT_CLEARED),
                ProviderRowOffer.SignIn,
            ),
            listOf(NO_FLOWS, mapOf("a-vendor" to false), ALL_FLOWS).map {
                ProviderRowDispatch.offerFor(vendor, it, ProviderHubGroup.SUBSCRIPTION)
            },
        )
    }

    // A carried flow does not make a row signable when the roster no longer
    // says the provider takes one. The catalog is served, so the two can
    // disagree between releases and the roster describes the provider.
    @Test
    fun `a compiled flow over a row that stopped offering OAUTH names no method`() {
        val keyOnly = row("a-vendor", methods = listOf(RosterAuthMethod.API_KEY))

        assertEquals(
            ProviderRowOffer.Blocked(ProviderRowOffer.Reason.NO_METHOD),
            ProviderRowDispatch.offerFor(keyOnly, ALL_FLOWS, ProviderHubGroup.SUBSCRIPTION),
        )
    }

    @Test
    fun `a refused row is refused from every group`() {
        val shut = row(
            "shut",
            methods = listOf(RosterAuthMethod.API_KEY, RosterAuthMethod.OAUTH),
            enabled = false,
            subscription = true,
        )

        assertEquals(
            listOf(
                ProviderRowOffer.Blocked(ProviderRowOffer.Reason.HELD_SHUT),
                ProviderRowOffer.Blocked(ProviderRowOffer.Reason.HELD_SHUT),
            ),
            listOf(ProviderHubGroup.SUBSCRIPTION, ProviderHubGroup.BRING_YOUR_OWN_KEY)
                .map { ProviderRowDispatch.offerFor(shut, mapOf("shut" to true), it) },
        )
    }

    @Test
    fun `the way in is one answer rather than a list of methods`() {
        assertEquals(
            listOf(
                ProviderWayIn.KEY,
                ProviderWayIn.PLAN,
                ProviderWayIn.KEY_OR_PLAN,
                ProviderWayIn.OWN_ENDPOINT,
                ProviderWayIn.NONE,
            ),
            listOf(
                row("keyed"),
                row("planned", methods = listOf(RosterAuthMethod.OAUTH)),
                row("both", methods = listOf(RosterAuthMethod.API_KEY, RosterAuthMethod.OAUTH)),
                row("mine", origin = RosterProviderOrigin.CUSTOM),
                row("empty", methods = emptyList()),
            ).map(ProviderRowDispatch::wayInFor),
        )
    }

    private fun hubRow(providerId: String, offer: ProviderRowOffer): ProviderHubRow =
        ProviderHubRow(
            providerId = providerId,
            displayName = providerId,
            group = ProviderHubGroup.BRING_YOUR_OWN_KEY,
            offer = offer,
            wayIn = ProviderWayIn.KEY,
            signingIn = false,
        )

    private companion object {
        /** No compiled flow at all — the vendor is absent from the map. */
        val NO_FLOWS = emptyMap<String, Boolean>()

        /** A flow the browser will start, which is the only offerable state. */
        val ALL_FLOWS = mapOf("a-vendor" to true)

        fun credential(
            state: RosterCredentialState = RosterCredentialState.USABLE,
            subscriptionBacked: Boolean = false,
        ) = RosterStoredCredential(
            authMethod = if (subscriptionBacked) {
                RosterAuthMethod.OAUTH
            } else {
                RosterAuthMethod.API_KEY
            },
            state = state,
            subscriptionBacked = subscriptionBacked,
            accountLabel = null,
            planLabel = null,
        )

        fun row(
            providerId: String,
            methods: List<RosterAuthMethod> = listOf(RosterAuthMethod.API_KEY),
            origin: RosterProviderOrigin = RosterProviderOrigin.CATALOG,
            catalogLayer: RosterCatalogLayer = RosterCatalogLayer.EMBEDDED_BASELINE,
            enabled: Boolean = true,
            configurable: Boolean = true,
            subscription: Boolean = false,
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
            catalogLayer = catalogLayer,
            selectedModelId = null,
            thinking = null,
            presentation = null,
            endpointBase = null,
            lastRefusal = null,
            modelCount = 0,
        )
    }
}
