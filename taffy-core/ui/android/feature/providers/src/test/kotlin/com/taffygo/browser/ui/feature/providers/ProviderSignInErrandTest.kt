// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.providerauth.ProviderFlowEvent
import com.taffygo.browser.ui.core.providerauth.ProviderFlowEventKind
import com.taffygo.browser.ui.core.providerauth.ProviderSignInCommands
import com.taffygo.browser.ui.core.providerauth.ProviderSignInEngine
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test

/**
 * Screen SCR-416's one relationship with the surface a vendor's own page is
 * drawn on: it opens one, it stays underneath it, and it takes it down when
 * the vendor has nothing left to do.
 *
 * Split from `ProviderSignInViewModelTest`, which is about the flow itself.
 * The seam is worth naming because the defect that produced these cases lived
 * exactly on it: the flow was correct and the surface was wrong.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ProviderSignInErrandTest {
    private val dispatcher = StandardTestDispatcher()
    private val roster = FakeProviderRoster()
    private val errand = FakeErrandPage()
    private val analytics = RecordingProviderAnalytics()
    private val navigator = RecordingProviderNavigator()
    private val engine = ProviderSignInEngine(
        object : ProviderSignInCommands {
            override suspend fun start(providerId: String): String = FLOW

            override suspend fun cancel(flowId: String) = Unit

            override suspend fun submitCode(flowId: String, entered: String): Boolean = true
        },
    )

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `the verification page opens as an errand and leaves this screen on the stack`() =
        runTest(dispatcher) {
            val viewModel = started(DEVICE_VENDOR)
            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()
            engine.onFlowEvent(
                ProviderFlowEvent(
                    providerId = DEVICE_VENDOR,
                    flowId = FLOW,
                    kind = ProviderFlowEventKind.USER_CODE_READY,
                    verificationUrl = "https://vendor.test/device",
                    userCode = "BDWN-XKQP",
                ),
            )
            runCurrent()

            viewModel.onIntent(ProviderSignInIntent.OpenVerificationPage, navigator)
            runCurrent()

            assertEquals(listOf("https://vendor.test/device"), errand.opened)
            // Nothing navigated here. Opening an errand is what shows it — the
            // browser decides that, so a page can never be opened somewhere
            // nobody is looking — and this screen stays underneath with its
            // countdown still running, which is what back comes back to.
            assertEquals(emptyList<TaffyDestination>(), navigator.visited)
        }

    /**
     * The defect a phone found on 2026-09-07: an Antigravity sign-in that had
     * actually succeeded looked stuck, because the throttle that claims the
     * redirect answers `CANCEL_AND_IGNORE` and leaves the vendor's page frozen
     * on screen, hiding this screen's own report that it worked.
     */
    @Test
    fun `claiming the code takes the vendor's page down`() = runTest(dispatcher) {
        val viewModel = started(REDIRECT_VENDOR)
        viewModel.onIntent(ProviderSignInIntent.Start, navigator)
        runCurrent()
        awaitingVendor()
        runCurrent()
        // A redirect vendor's authorization page is opened by the browser, so
        // stand one up the way it would already be standing.
        val opened = errand.open("https://vendor.test/authorize")
        runCurrent()
        assertEquals(opened, errand.page.value?.errandId)

        engine.onFlowEvent(
            ProviderFlowEvent(REDIRECT_VENDOR, FLOW, ProviderFlowEventKind.EXCHANGING),
        )
        runCurrent()

        assertNull(errand.page.value)
    }

    @Test
    fun `a refusal takes the vendor's page down too`() = runTest(dispatcher) {
        val viewModel = started(REDIRECT_VENDOR)
        viewModel.onIntent(ProviderSignInIntent.Start, navigator)
        runCurrent()
        awaitingVendor()
        runCurrent()
        errand.open("https://vendor.test/authorize")
        runCurrent()

        engine.onFlowEvent(
            ProviderFlowEvent(REDIRECT_VENDOR, FLOW, ProviderFlowEventKind.FAILED_DENIED),
        )
        runCurrent()

        assertNull(errand.page.value)
    }

    /**
     * Only the page this flow was waiting on is taken down. An errand a person
     * opened while nothing was running — a "get a key" link, say — is not this
     * screen's to close, so a state change that never passed through waiting
     * must not reach for one.
     */
    @Test
    fun `an errand opened outside a flow is left alone`() = runTest(dispatcher) {
        started(REDIRECT_VENDOR)
        errand.open("https://vendor.test/keys")
        runCurrent()

        engine.onFlowEvent(
            ProviderFlowEvent(REDIRECT_VENDOR, FLOW, ProviderFlowEventKind.EXCHANGING),
        )
        runCurrent()

        assertEquals("errand-1", errand.page.value?.errandId)
    }

    /** The browser's report for a tab open on the vendor's page: no code to show. */
    private fun awaitingVendor() {
        engine.onFlowEvent(
            ProviderFlowEvent(REDIRECT_VENDOR, FLOW, ProviderFlowEventKind.AWAITING_AUTHORIZATION),
        )
    }

    private fun TestScope.started(providerId: String): ProviderSignInViewModel {
        roster.publish(
            providerRow(providerId, authMethods = listOf(RosterAuthMethod.OAUTH), stored = null),
        )
        val viewModel = ProviderSignInViewModel(
            roster,
            engine,
            errand,
            analytics,
            SavedStateHandle(mapOf(TaffyDestination.PROVIDER_ID to providerId)),
            signInFlows = mapOf(DEVICE_VENDOR to true, REDIRECT_VENDOR to true),
            pkceVendors = setOf(REDIRECT_VENDOR),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        return viewModel
    }

    private companion object {
        /** A compiled vendor whose flow shows a code. */
        const val DEVICE_VENDOR = "xai"

        /** A compiled vendor whose flow returns through a redirect. */
        const val REDIRECT_VENDOR = "openai"
        const val FLOW = "provider-flow-test"
    }
}
