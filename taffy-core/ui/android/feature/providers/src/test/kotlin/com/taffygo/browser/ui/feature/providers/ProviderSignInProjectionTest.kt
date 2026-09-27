// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFailure
import com.taffygo.browser.ui.core.providerauth.ProviderSignInState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Every stage screen SCR-416 can be in, reached without a vendor.
 *
 * That is the point of the suite. The surface this screen replaces could only
 * be seen by signing in to a real vendor with a real plan, so nobody ever
 * looked at what it did after the code appeared — and what it did was nothing.
 */
class ProviderSignInProjectionTest {

    /** One vendor whose flow this binary carries and the browser will start. */
    private val vendors = mapOf("plan-vendor" to true)

    // A flow that is built and one that is missing are different facts with
    // different remedies, and only one of them is answered by an update. The
    // hub does not offer either as a Sign in, but a restored back stack can
    // land here, and the page has to say the true one.
    @Test
    fun `a built sign-in that may not start is said apart from a missing one`() {
        val row = providerRow("plan-vendor", authMethods = listOf(RosterAuthMethod.OAUTH))

        assertEquals(
            listOf(
                ProviderSignInUiState.Status.READY,
                ProviderSignInUiState.Status.NOT_CLEARED,
                ProviderSignInUiState.Status.NOT_BUILT,
            ),
            listOf(
                mapOf("plan-vendor" to true),
                mapOf("plan-vendor" to false),
                emptyMap(),
            ).map { flows ->
                ProviderSignInProjection.project(
                    providerId = "plan-vendor",
                    roster = ProviderRosterState(ready = true, rows = listOf(row)),
                    engine = null,
                    flowRan = false,
                    cancelled = false,
                    codeShownForSeconds = 0,
                    signInFlows = flows,
                ).status
            },
        )
    }

    // A row that offers no plan at all reads as not built whatever the flow
    // map says: "the sign-in is not cleared yet" would be a promise about a
    // provider that never offered one.
    @Test
    fun `a row offering no plan is not built even where a flow is carried`() {
        val keyOnly = providerRow("plan-vendor", authMethods = listOf(RosterAuthMethod.API_KEY))

        assertEquals(
            ProviderSignInUiState.Status.NOT_BUILT,
            ProviderSignInProjection.project(
                providerId = "plan-vendor",
                roster = ProviderRosterState(ready = true, rows = listOf(keyOnly)),
                engine = null,
                flowRan = false,
                cancelled = false,
                codeShownForSeconds = 0,
                signInFlows = mapOf("plan-vendor" to false),
            ).status,
        )
    }

    @Test
    fun `a roster that has not published yet is still coming`() {
        val state = project(ProviderRosterState(), engine = null)

        assertEquals(ProviderSignInUiState.Status.LOADING, state.status)
    }

    @Test
    fun `a vendor this build compiles no flow for is explained, not offered`() {
        val row = providerRow("unbuilt", authMethods = listOf(RosterAuthMethod.OAUTH))

        val state = project(ready(row), engine = null, providerId = "unbuilt")

        assertEquals(ProviderSignInUiState.Status.NOT_BUILT, state.status)
        assertFalse(state.startable)
    }

    @Test
    fun `a provider held shut offers no sign-in even where the flow is compiled`() {
        val row = planVendor().copy(enabled = false)

        val state = project(ready(row), engine = null)

        assertEquals(ProviderSignInUiState.Status.NOT_BUILT, state.status)
    }

    @Test
    fun `nothing running is idle and startable`() {
        val state = project(ready(planVendor()), engine = null)

        assertEquals(ProviderSignInStage.Idle, state.stage)
        assertTrue(state.startable)
        assertFalse(state.running)
    }

    @Test
    fun `the engine's states map one for one onto the page's`() {
        val cases = listOf(
            ProviderSignInState.Starting to ProviderSignInStage.Starting,
            ProviderSignInState.Exchanging to ProviderSignInStage.Exchanging,
            ProviderSignInState.Failed(ProviderSignInFailure.DENIED) to
                ProviderSignInStage.Failed(ProviderSignInFailure.DENIED),
        )

        cases.forEach { (engine, stage) ->
            assertEquals(stage, project(ready(planVendor()), engine = engine).stage)
        }
    }

    @Test
    fun `an authorization with no code is a wait, and one with a code is a code`() {
        val bare = ProviderSignInState.AwaitingAuthorization(null, null)
        val coded = ProviderSignInState.AwaitingAuthorization("https://vendor.test/dev", "BDWN-XKQP")

        assertEquals(ProviderSignInStage.Waiting(null), project(ready(planVendor()), bare).stage)
        assertEquals(
            ProviderSignInStage.CodeReady(
                verificationUrl = "https://vendor.test/dev",
                userCode = "BDWN-XKQP",
                remainingSeconds = ProviderSignInProjection.WAITING_WINDOW_SECONDS,
            ),
            project(ready(planVendor()), coded).stage,
        )
    }

    @Test
    fun `half a device flow is a wait rather than a panel nobody can act on`() {
        val urlOnly = ProviderSignInState.AwaitingAuthorization("https://vendor.test/dev", null)
        val codeOnly = ProviderSignInState.AwaitingAuthorization(null, "BDWN-XKQP")

        assertEquals(ProviderSignInStage.Waiting(null), project(ready(planVendor()), urlOnly).stage)
        assertEquals(ProviderSignInStage.Waiting(null), project(ready(planVendor()), codeOnly).stage)
    }

    // Decision 0095 section 2: a redirect can fail to come back, so a wait on
    // one carries a place to paste what the vendor showed. A device-code flow
    // has no redirect and its wait carries nothing — the field would be a
    // control that hands the browser something it will refuse.
    @Test
    fun `a redirect wait carries the manual-code field and a device wait does not`() {
        val bare = ProviderSignInState.AwaitingAuthorization(null, null)

        val redirect = project(ready(planVendor()), bare, pkce = setOf("plan-vendor"))
        val device = project(ready(planVendor()), bare, pkce = emptySet())

        assertEquals(ProviderSignInStage.Waiting(ManualCodeEntry()), redirect.stage)
        assertEquals(ProviderSignInStage.Waiting(null), device.stage)
    }

    @Test
    fun `the manual-code draft, its refusal and its submission flow through the wait`() {
        val bare = ProviderSignInState.AwaitingAuthorization(null, null)
        val entry = ManualCodeEntry(draft = "abc#def", rejected = true, submitting = false)

        val state = project(
            ready(planVendor()),
            bare,
            manualCode = entry,
            pkce = setOf("plan-vendor"),
        )
        val sending = project(
            ready(planVendor()),
            bare,
            manualCode = entry.copy(rejected = false, submitting = true),
            pkce = setOf("plan-vendor"),
        )

        assertEquals(ProviderSignInStage.Waiting(entry), state.stage)
        assertTrue(state.running)
        assertFalse((sending.stage as ProviderSignInStage.Waiting).codeEntry?.submittable == true)
        assertTrue(entry.submittable)
        assertFalse(ManualCodeEntry(draft = "   ").submittable)
    }

    // The draft is this screen's alone; it does not leak onto a stage that has
    // nowhere to put it. A code panel is a device flow, which never takes one.
    @Test
    fun `a manual-code draft does not reach the code panel or an exchange`() {
        val coded = ProviderSignInState.AwaitingAuthorization("https://vendor.test/dev", "AAAA")
        val entry = ManualCodeEntry(draft = "stale")

        assertTrue(
            project(ready(planVendor()), coded, manualCode = entry, pkce = setOf("plan-vendor"))
                .stage is ProviderSignInStage.CodeReady,
        )
        assertEquals(
            ProviderSignInStage.Exchanging,
            project(
                ready(planVendor()),
                ProviderSignInState.Exchanging,
                manualCode = entry,
                pkce = setOf("plan-vendor"),
            ).stage,
        )
    }

    @Test
    fun `the waiting window counts down and stops at zero`() {
        val coded = ProviderSignInState.AwaitingAuthorization("https://vendor.test/dev", "AAAA")

        val part = project(ready(planVendor()), coded, codeShownForSeconds = 61)
        val over = project(
            ready(planVendor()),
            coded,
            codeShownForSeconds = ProviderSignInProjection.WAITING_WINDOW_SECONDS + 500,
        )

        assertEquals(
            ProviderSignInProjection.WAITING_WINDOW_SECONDS - 61,
            (part.stage as ProviderSignInStage.CodeReady).remainingSeconds,
        )
        assertEquals(0, (over.stage as ProviderSignInStage.CodeReady).remainingSeconds)
    }

    @Test
    fun `a credential that arrives after this screen started a flow is the success`() {
        val row = planVendor().copy(stored = storedCredential(RosterAuthMethod.OAUTH))

        val state = project(ready(row), engine = null, flowRan = true)

        assertEquals(ProviderSignInStage.Succeeded, state.stage)
        assertTrue(state.connected)
    }

    @Test
    fun `arriving at a provider already connected is idle, not a finished journey`() {
        val row = planVendor().copy(stored = storedCredential(RosterAuthMethod.OAUTH))

        val state = project(ready(row), engine = null, flowRan = false)

        assertEquals(ProviderSignInStage.Idle, state.stage)
        assertTrue(state.connected)
        assertTrue(state.startable)
    }

    @Test
    fun `accepted cancellation shows immediately and an exact report still wins`() {
        val cancelled = project(ready(planVendor()), engine = null, cancelled = true)
        val connectedCancelled = project(
            ready(planVendor().copy(stored = storedCredential(RosterAuthMethod.OAUTH))),
            engine = null,
            flowRan = true,
            cancelled = true,
        )
        val reported = project(
            ready(planVendor()),
            engine = ProviderSignInState.Failed(ProviderSignInFailure.TIMED_OUT),
            cancelled = true,
        )

        assertEquals(ProviderSignInStage.Cancelled, cancelled.stage)
        assertEquals(ProviderSignInStage.Cancelled, connectedCancelled.stage)
        assertEquals(
            ProviderSignInStage.Failed(ProviderSignInFailure.TIMED_OUT),
            reported.stage,
        )
    }

    @Test
    fun `every running stage offers a way out and none of them is startable`() {
        val running = listOf(
            ProviderSignInState.Starting,
            ProviderSignInState.AwaitingAuthorization("https://vendor.test/dev", "AAAA"),
            ProviderSignInState.AwaitingAuthorization(null, null),
            ProviderSignInState.Exchanging,
        )

        running.forEach { engine ->
            val state = project(ready(planVendor()), engine)
            assertTrue(state.running)
            assertFalse(state.startable)
        }
    }

    private fun planVendor(): ProviderRosterRow =
        providerRow("plan-vendor", authMethods = listOf(RosterAuthMethod.OAUTH))

    private fun ready(vararg rows: ProviderRosterRow) =
        ProviderRosterState(ready = true, rows = rows.toList())

    private fun project(
        roster: ProviderRosterState,
        engine: ProviderSignInState?,
        providerId: String = "plan-vendor",
        flowRan: Boolean = false,
        cancelled: Boolean = false,
        codeShownForSeconds: Int = 0,
        manualCode: ManualCodeEntry = ManualCodeEntry(),
        // Stated per case rather than read from the shipping map: which
        // vendors return through a redirect is the `catalog` lane's question.
        pkce: Set<String> = emptySet(),
    ) = ProviderSignInProjection.project(
        providerId = providerId,
        roster = roster,
        engine = engine,
        flowRan = flowRan,
        cancelled = cancelled,
        codeShownForSeconds = codeShownForSeconds,
        manualCode = manualCode,
        signInFlows = vendors,
        pkceVendors = pkce,
    )
}
