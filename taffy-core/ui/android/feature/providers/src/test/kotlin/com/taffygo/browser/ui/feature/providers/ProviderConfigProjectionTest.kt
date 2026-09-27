// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ModelInputModality
import com.taffygo.browser.ui.core.model.ModelRole
import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderPresentation
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderRefusal
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-415's whole surface, driven from published values.
 *
 * Every case here is one a person actually reaches — a provider that takes
 * both, a provider that takes neither, a sign-in this build cannot run, a
 * credential the vendor stopped confirming — and none of them needs a device
 * or a network to reach.
 */
class ProviderConfigProjectionTest {

    /** One vendor whose flow this binary carries and the browser will start. */
    private val vendors = mapOf("plan-vendor" to true)

    @Test
    fun `a roster that has not published yet is still coming, never gone`() {
        val state = project(ProviderRosterState(), providerId = "anything")

        assertEquals(ProviderConfigUiState.Status.LOADING, state.status)
    }

    @Test
    fun `a provider the roster does not carry is gone rather than loading`() {
        val state = project(ready(providerRow("other")), providerId = "missing")

        assertEquals(ProviderConfigUiState.Status.UNKNOWN, state.status)
    }

    @Test
    fun `a key provider gets a form and no sign-in block`() {
        val state = project(ready(providerRow("keyed")), providerId = "keyed")

        assertNotNull(state.keyForm)
        assertEquals(ProviderSignInOffer.NONE, state.signIn)
        assertTrue(!state.separatesWaysIn)
    }

    @Test
    fun `a provider offering both gets the sign-in above and the rule between`() {
        val row = providerRow(
            "plan-vendor",
            authMethods = listOf(RosterAuthMethod.API_KEY, RosterAuthMethod.OAUTH),
        )

        val state = project(ready(row), providerId = "plan-vendor")

        assertEquals(ProviderSignInOffer.OFFERED, state.signIn)
        assertNotNull(state.keyForm)
        assertTrue(state.separatesWaysIn)
    }

    @Test
    fun `a plan-only vendor this build cannot run is explained and gets no key form`() {
        val row = providerRow("unbuilt", authMethods = listOf(RosterAuthMethod.OAUTH))

        val state = project(ready(row), providerId = "unbuilt")

        assertEquals(ProviderSignInOffer.NOT_BUILT, state.signIn)
        assertNull(state.keyForm)
    }

    @Test
    fun `each roster refusal closes the whole page and names itself`() {
        val cases = mapOf(
            providerRow("a", configurable = false) to ProviderRowOffer.Reason.NOT_ACTIONABLE,
            providerRow("c", enabled = false) to ProviderRowOffer.Reason.HELD_SHUT,
            providerRow("d", authMethods = emptyList()) to ProviderRowOffer.Reason.NO_METHOD,
        )

        cases.forEach { (row, reason) ->
            val state = project(ready(row), providerId = row.providerId)
            assertEquals(reason, state.blocked)
            assertNull(state.keyForm)
            assertEquals(ProviderSignInOffer.NONE, state.signIn)
            assertTrue(!state.actionable)
        }
    }

    @Test
    fun `the served prefix refuses a draft before a probe is spent on it`() {
        val row = providerRow(
            "keyed",
            presentation = ProviderPresentation(keyPrefix = "tk-", getKeyUrl = null, docsUrl = null),
        )

        val wrong = project(
            ready(row),
            providerId = "keyed",
            draft = ProviderConfigDraft(key = "zz-1234"),
        )
        val right = project(
            ready(row),
            providerId = "keyed",
            draft = ProviderConfigDraft(key = "tk-1234"),
        )

        assertEquals(ProviderKeyForm.Verdict.PREFIX_MISMATCH, wrong.keyForm?.verdict)
        assertTrue(wrong.keyForm?.actionable == false)
        assertEquals(ProviderKeyForm.Verdict.PLAUSIBLE, right.keyForm?.verdict)
        assertTrue(right.keyForm?.actionable == true)
    }

    @Test
    fun `with no prefix served a non-empty draft is plausible and nothing is invented`() {
        val state = project(
            ready(providerRow("keyed")),
            providerId = "keyed",
            draft = ProviderConfigDraft(key = "x"),
        )

        assertEquals(ProviderKeyForm.Verdict.PLAUSIBLE, state.keyForm?.verdict)
    }

    @Test
    fun `a stored credential makes the action a replacement`() {
        val row = providerRow("keyed", stored = storedCredential())

        val state = project(ready(row), providerId = "keyed")

        assertTrue(state.keyForm?.replacing == true)
    }

    @Test
    fun `the card names the pinned model by the catalog's own name`() {
        val row = providerRow("keyed", stored = storedCredential(), selectedModelId = "m-2")
        val state = project(
            ready(row),
            providerId = "keyed",
            models = mapOf("keyed" to listOf(model("m-1", "First"), model("m-2", "Second"))),
        )

        assertEquals("Second", state.managed?.modelName)
    }

    @Test
    fun `a pin the catalog no longer carries shows as no pin at all`() {
        val row = providerRow("keyed", stored = storedCredential(), selectedModelId = "gone")

        val state = project(
            ready(row),
            providerId = "keyed",
            models = mapOf("keyed" to listOf(model("m-1", "First"))),
        )

        assertNull(state.managed?.modelName)
    }

    @Test
    fun `a credential the vendor stopped confirming stays on the page and says so`() {
        val row = providerRow(
            "keyed",
            stored = storedCredential(state = RosterCredentialState.REFRESH_FAILED),
        )

        val state = project(ready(row), providerId = "keyed")

        assertNotNull(state.managed)
        assertTrue(state.managed?.confirmed == false)
    }

    // A refusal is a fact about a request, not about the credential: the card
    // stays confirmed, keeps every action, and says what the vendor declined
    // beside the model line (registry document section 5.4).
    @Test
    fun `the card carries the vendor's last refusal without unconfirming the credential`() {
        val refused = refusal(RosterProviderRefusal.RATE_LIMIT)
        val row = providerRow("keyed", stored = storedCredential(), lastRefusal = refused)

        val state = project(ready(row), providerId = "keyed")

        assertEquals(refused, state.managed?.lastRefusal)
        assertTrue(state.managed?.confirmed == true)
        assertNull(project(ready(providerRow("keyed", stored = storedCredential())), "keyed")
            .managed?.lastRefusal)
    }

    @Test
    fun `signing in again is offered only where this build compiles the flow`() {
        val compiled = providerRow(
            "plan-vendor",
            authMethods = listOf(RosterAuthMethod.OAUTH),
            stored = storedCredential(authMethod = RosterAuthMethod.OAUTH),
        )
        val pasted = providerRow("keyed", stored = storedCredential())

        assertTrue(project(ready(compiled), "plan-vendor").managed?.canReauthenticate == true)
        assertTrue(project(ready(pasted), "keyed").managed?.canReauthenticate == false)
    }

    @Test
    fun `a key the browser holds and the core has not echoed is still manageable`() {
        // The state every provider whose models are fetched rather than
        // published is in on each browser start, and the one the connected
        // list has always called connected. The page must agree with it: a
        // card the person can sign out from and pin a model on, saying
        // unconfirmed rather than drawing the bare paste form back over a
        // credential that is on this phone.
        val state = project(
            ready(providerRow("keyed")),
            providerId = "keyed",
            held = setOf("keyed"),
        )

        assertNotNull(state.managed)
        assertTrue(state.managed?.confirmed == false)
        assertNull(state.managed?.accountLabel)
        assertNull(state.managed?.planLabel)
        assertTrue(state.managed?.subscriptionBacked == false)
    }

    @Test
    fun `a provider with no credential anywhere has no card to manage`() {
        val state = project(ready(providerRow("keyed")), providerId = "keyed")

        assertNull(state.managed)
    }

    @Test
    fun `with nothing stored there is nothing to make default`() {
        val state = project(ready(providerRow("keyed")), providerId = "keyed")

        assertEquals(ProviderDefaultChoice.UNAVAILABLE, state.defaultChoice)
    }

    @Test
    fun `one usable credential off the managed route is the standing choice`() {
        val row = providerRow("keyed", stored = storedCredential())

        val state = project(
            ready(row),
            providerId = "keyed",
        )

        assertEquals(ProviderDefaultChoice.IN_FORCE, state.defaultChoice)
    }

    @Test
    fun `two credentials leave the product naming neither`() {
        val roster = ready(
            providerRow("first", stored = storedCredential()),
            providerRow("second", stored = storedCredential()),
        )

        val state = project(roster, providerId = "first", route = ProviderRoute.DIRECT_WITH_YOUR_KEY)

        assertEquals(ProviderDefaultChoice.SHARED, state.defaultChoice)
    }

    private fun ready(vararg rows: ProviderRosterRow) =
        ProviderRosterState(ready = true, rows = rows.toList())

    private fun project(
        roster: ProviderRosterState,
        providerId: String,
        models: Map<String, List<ProviderModel>> = emptyMap(),
        held: Set<String> = emptySet(),
        route: ProviderRoute = ProviderRoute.NOT_CONFIGURED,
        draft: ProviderConfigDraft = ProviderConfigDraft(),
    ) = ProviderConfigProjection.project(
        providerId = providerId,
        roster = roster,
        models = models,
        browserHeldCredentialIds = held,
        draft = draft,
        signInFlows = vendors,
    )

    private fun model(modelId: String, displayName: String) = ProviderModel(
        providerId = "keyed",
        modelId = modelId,
        displayName = displayName,
        contextWindow = 1u,
        maxOutputTokens = 1u,
        reasoning = false,
        toolCalling = false,
        roles = listOf(ModelRole.PRIMARY_REASONING),
        inputModalities = listOf(ModelInputModality.TEXT),
        thinkingLevels = emptyList(),
    )
}
