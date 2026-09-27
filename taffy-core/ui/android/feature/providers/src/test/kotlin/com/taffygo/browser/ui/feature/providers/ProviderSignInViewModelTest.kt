// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.providerauth.ProviderFlowEvent
import com.taffygo.browser.ui.core.providerauth.ProviderFlowEventKind
import com.taffygo.browser.ui.core.providerauth.ProviderSignInEngine
import com.taffygo.browser.ui.core.providerauth.ProviderSignInCommands
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Screen SCR-416 driven end to end without a vendor.
 *
 * The two vendor identifiers below are the binary's own, not the catalog's:
 * `ProviderSignInFlows` is compiled (decision 0081), so a suite that exercises
 * a flow shape has to name a vendor that carries it. Which vendors those are
 * is `ProviderSignInFlowsParityTest`'s question.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ProviderSignInViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val roster = FakeProviderRoster()
    private val errand = FakeErrandPage()
    private val analytics = RecordingProviderAnalytics()
    private val navigator = RecordingProviderNavigator()
    private val admitted = mutableListOf<String>()
    private val cancelled = mutableListOf<String>()
    private val submitted = mutableListOf<Pair<String, String>>()
    private var acceptCode = true
    private val engine = ProviderSignInEngine(
        object : ProviderSignInCommands {
            override suspend fun start(providerId: String): String {
                admitted += providerId
                return FLOW
            }

            override suspend fun cancel(flowId: String) {
                cancelled += flowId
            }

            override suspend fun submitCode(flowId: String, entered: String): Boolean {
                submitted += flowId to entered
                return acceptCode
            }
        },
    )

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `starting admits the flow and the code panel follows the browser's report`() =
        runTest(dispatcher) {
            val viewModel = started(DEVICE_VENDOR)

            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()
            assertEquals(listOf(DEVICE_VENDOR), admitted)
            assertEquals(ProviderSignInStage.Starting, viewModel.state.value.stage)

            codeReady()
            runCurrent()

            assertEquals(
                ProviderSignInStage.CodeReady(
                    verificationUrl = "https://vendor.test/device",
                    userCode = "BDWN-XKQP",
                    remainingSeconds = ProviderSignInProjection.WAITING_WINDOW_SECONDS,
                ),
                viewModel.state.value.stage,
            )
        }

    @Test
    fun `the countdown runs while the code is up`() = runTest(dispatcher) {
        val viewModel = started(DEVICE_VENDOR)
        viewModel.onIntent(ProviderSignInIntent.Start, navigator)
        runCurrent()
        codeReady()
        runCurrent()

        advanceTimeBy(61_000)
        runCurrent()

        assertEquals(
            ProviderSignInProjection.WAITING_WINDOW_SECONDS - 61,
            (viewModel.state.value.stage as ProviderSignInStage.CodeReady).remainingSeconds,
        )
    }

    @Test
    fun `the window running out cancels the exact flow`() =
        runTest(dispatcher) {
            val viewModel = started(DEVICE_VENDOR)
            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()
            codeReady()
            runCurrent()

            advanceTimeBy(
                ProviderSignInProjection.WAITING_WINDOW_SECONDS.toLong() * 1_000 + 2_000,
            )
            runCurrent()

            assertEquals(ProviderSignInStage.Cancelled, viewModel.state.value.stage)
            assertEquals(listOf(FLOW), cancelled)
        }

    @Test
    fun `cancel sends the exact flow and only then shows cancelled`() =
        runTest(dispatcher) {
            val viewModel = started(DEVICE_VENDOR)
            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()
            codeReady()
            runCurrent()

            viewModel.onIntent(ProviderSignInIntent.Cancel, navigator)
            runCurrent()

            assertEquals(ProviderSignInStage.Cancelled, viewModel.state.value.stage)
            assertTrue(viewModel.state.value.startable)
            assertEquals(listOf(FLOW), cancelled)
        }

    @Test
    fun `a late terminal from an accepted cancellation is ignored`() =
        runTest(dispatcher) {
            val viewModel = started(DEVICE_VENDOR)
            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()
            codeReady()
            runCurrent()
            viewModel.onIntent(ProviderSignInIntent.Cancel, navigator)
            runCurrent()

            engine.onFlowEvent(
                ProviderFlowEvent(DEVICE_VENDOR, FLOW, ProviderFlowEventKind.FAILED_DEADLINE),
            )
            runCurrent()

            assertEquals(ProviderSignInStage.Cancelled, viewModel.state.value.stage)
        }

    @Test
    fun `a credential filed after this screen started the flow is the success`() =
        runTest(dispatcher) {
            val viewModel = started(DEVICE_VENDOR)
            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()

            engine.onFlowEvent(
                ProviderFlowEvent(DEVICE_VENDOR, FLOW, ProviderFlowEventKind.COMPLETED),
            )
            publish(DEVICE_VENDOR, connected = true)
            runCurrent()

            assertEquals(ProviderSignInStage.Succeeded, viewModel.state.value.stage)
        }

    @Test
    fun `going on from a finished sign-in opens the provider's own page`() =
        runTest(dispatcher) {
            val viewModel = started(DEVICE_VENDOR)

            viewModel.onIntent(ProviderSignInIntent.OpenProviderPage, navigator)

            assertEquals(
                listOf(TaffyDestination.ProviderConfig(DEVICE_VENDOR)),
                navigator.visited,
            )
        }

    // Decision 0095 section 2: the redirect did not come back, the person
    // pasted what the vendor showed, and the browser is handed it whole for
    // the exact flow. A no is a caution on the field; a yes is nothing of this
    // screen's until the browser reports the exchange, as for any redirect.
    @Test
    fun `a manual code is handed to the browser for the exact flow and a refusal is said`() =
        runTest(dispatcher) {
            val viewModel = started(REDIRECT_VENDOR)
            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()
            redirectWait(REDIRECT_VENDOR)
            runCurrent()
            assertEquals(
                ProviderSignInStage.Waiting(ManualCodeEntry()),
                viewModel.state.value.stage,
            )

            acceptCode = false
            viewModel.onIntent(ProviderSignInIntent.ManualCodeChanged("half-a-co"), navigator)
            viewModel.onIntent(ProviderSignInIntent.SubmitManualCode, navigator)
            runCurrent()

            assertEquals(listOf(FLOW to "half-a-co"), submitted)
            assertEquals(
                ProviderSignInStage.Waiting(
                    ManualCodeEntry(draft = "half-a-co", rejected = true, submitting = false),
                ),
                viewModel.state.value.stage,
            )

            // Typing again withdraws the refusal: it was about the old value.
            viewModel.onIntent(ProviderSignInIntent.ManualCodeChanged("half-a-code"), navigator)
            runCurrent()
            assertEquals(
                ProviderSignInStage.Waiting(ManualCodeEntry(draft = "half-a-code")),
                viewModel.state.value.stage,
            )
        }

    @Test
    fun `an accepted manual code clears the draft and the browser's exchange moves the page`() =
        runTest(dispatcher) {
            val viewModel = started(REDIRECT_VENDOR)
            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()
            redirectWait(REDIRECT_VENDOR)
            runCurrent()

            viewModel.onIntent(
                ProviderSignInIntent.ManualCodeChanged("https://vendor.test/cb?code=x&state=y"),
                navigator,
            )
            viewModel.onIntent(ProviderSignInIntent.SubmitManualCode, navigator)
            runCurrent()

            assertEquals(listOf(FLOW to "https://vendor.test/cb?code=x&state=y"), submitted)
            assertEquals(
                "acceptance is not a state of this screen; the field simply empties",
                ProviderSignInStage.Waiting(ManualCodeEntry()),
                viewModel.state.value.stage,
            )

            engine.onFlowEvent(
                ProviderFlowEvent(REDIRECT_VENDOR, FLOW, ProviderFlowEventKind.EXCHANGING),
            )
            runCurrent()
            assertEquals(ProviderSignInStage.Exchanging, viewModel.state.value.stage)
        }

    @Test
    fun `a blank draft and a device-code wait hand the browser nothing`() =
        runTest(dispatcher) {
            val viewModel = started(DEVICE_VENDOR)
            viewModel.onIntent(ProviderSignInIntent.Start, navigator)
            runCurrent()
            redirectWait(DEVICE_VENDOR)
            runCurrent()
            assertEquals(ProviderSignInStage.Waiting(null), viewModel.state.value.stage)

            viewModel.onIntent(ProviderSignInIntent.ManualCodeChanged("   "), navigator)
            viewModel.onIntent(ProviderSignInIntent.SubmitManualCode, navigator)
            runCurrent()

            assertTrue(submitted.isEmpty())
        }

    // The browser refuses anything over its bound, so the field stops there
    // rather than accepting a paste it is about to be told no about.
    @Test
    fun `an entry past the browser's bound is left as it was`() = runTest(dispatcher) {
        val viewModel = started(REDIRECT_VENDOR)
        viewModel.onIntent(ProviderSignInIntent.Start, navigator)
        runCurrent()
        redirectWait(REDIRECT_VENDOR)
        runCurrent()

        viewModel.onIntent(ProviderSignInIntent.ManualCodeChanged("fits"), navigator)
        viewModel.onIntent(ProviderSignInIntent.ManualCodeChanged("x".repeat(8 * 1024 + 1)), navigator)
        runCurrent()

        assertEquals(
            ProviderSignInStage.Waiting(ManualCodeEntry(draft = "fits")),
            viewModel.state.value.stage,
        )
    }

    @Test
    fun `the screen records itself as SCR-416`() = runTest(dispatcher) {
        started(DEVICE_VENDOR).onShown()

        assertEquals(
            listOf(
                AnalyticsEvent.ScreenShown(
                    TaffyDestination.ProviderSignIn(DEVICE_VENDOR).screenId,
                ),
            ),
            analytics.recorded,
        )
    }

    private fun codeReady() {
        engine.onFlowEvent(
            ProviderFlowEvent(
                providerId = DEVICE_VENDOR,
                flowId = FLOW,
                kind = ProviderFlowEventKind.USER_CODE_READY,
                verificationUrl = "https://vendor.test/device",
                userCode = "BDWN-XKQP",
            ),
        )
    }

    /** The browser's report for a tab open on the vendor's page: no code to show. */
    private fun redirectWait(providerId: String) {
        engine.onFlowEvent(
            ProviderFlowEvent(
                providerId = providerId,
                flowId = FLOW,
                kind = ProviderFlowEventKind.AWAITING_AUTHORIZATION,
            ),
        )
    }

    private fun publish(providerId: String, connected: Boolean) {
        roster.publish(
            providerRow(
                providerId,
                authMethods = listOf(RosterAuthMethod.OAUTH),
                stored = if (connected) {
                    storedCredential(authMethod = RosterAuthMethod.OAUTH)
                } else {
                    null
                },
            ),
        )
    }

    private fun TestScope.started(providerId: String): ProviderSignInViewModel {
        publish(providerId, connected = false)
        val viewModel = ProviderSignInViewModel(
            roster,
            engine,
            errand,
            analytics,
            SavedStateHandle(mapOf(TaffyDestination.PROVIDER_ID to providerId)),
            // Stated rather than read from the shipping map: this suite is
            // about what the screen does once a flow may run, and which
            // vendors may run one is a fact the `catalog` lane owns. Reading
            // the real map made every case here depend on whose terms review
            // happened to be dated.
            signInFlows = mapOf(
                DEVICE_VENDOR to true,
                REDIRECT_VENDOR to true,
            ),
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
