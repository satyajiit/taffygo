// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
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
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Screen SCR-418's three consequential paths, and the one that makes this
 * screen different from a form.
 *
 * **The hole this screen closes.** A person types the bare origin their server
 * is on. The probe reaches it — the runtime's own discovery path answers there
 * — and reports Ollama with four models. Every request afterwards would fail,
 * because the transport joins the operation beneath the address they typed and
 * `…:11434/chat/completions` is not a path any runtime serves. The screen must
 * therefore propose the corrected address and have it answered before it will
 * save, and it must never make the correction itself: the register's question
 * is "did this person type this?" (decision 0096 section 1), and rewriting the
 * address makes the answer no.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class CustomEndpointViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val roster = FakeProviderRoster()
    private val endpoints = FakeCustomEndpoints()
    private val credentials = FakeProviderCredentials()
    private val analytics = RecordingProviderAnalytics()
    private val navigator = RecordingProviderNavigator()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `a bare origin that names a runtime proposes the corrected address`() =
        runTest(dispatcher) {
            endpoints.answer = ollamaWithFour()
            val viewModel = adding()

            probe(viewModel, "http://192.168.1.9:11434")

            val state = viewModel.state.value
            assertEquals(
                listOf(FakeCustomEndpoints.Ask("http://192.168.1.9:11434", "192-168-1-9")),
                endpoints.probed,
            )
            assertEquals("http://192.168.1.9:11434/v1", state.proposal)
            // Proposed, never applied: the field still holds what was typed.
            assertEquals("http://192.168.1.9:11434", state.address)
            assertTrue(state.proposalUnanswered)
        }

    @Test
    fun `an unanswered proposal is not saved, whatever else is filled in`() =
        runTest(dispatcher) {
            endpoints.answer = ollamaWithFour()
            val viewModel = adding()
            probe(viewModel, "http://192.168.1.9:11434")
            fill(viewModel, name = "The laptop")

            viewModel.onIntent(CustomEndpointIntent.Save, navigator)
            runCurrent()

            assertFalse(viewModel.state.value.saveActionable)
            assertTrue(endpoints.saved.isEmpty())
        }

    @Test
    fun `accepting the proposal is what puts the corrected address in the write`() =
        runTest(dispatcher) {
            endpoints.answer = ollamaWithFour()
            val viewModel = adding()
            probe(viewModel, "http://192.168.1.9:11434")
            fill(viewModel, name = "The laptop")

            viewModel.onIntent(CustomEndpointIntent.AcceptProposal, navigator)
            runCurrent()
            viewModel.onIntent(CustomEndpointIntent.Save, navigator)
            runCurrent()

            assertEquals(
                listOf(
                    FakeCustomEndpoints.Write(
                        // The identity the probe already named, reused rather
                        // than minted a second time, so the verdict on the page
                        // and the provider this write produced are one row.
                        providerId = "192-168-1-9",
                        displayName = "The laptop",
                        address = "http://192.168.1.9:11434/v1",
                        // The probe's own specs, whole: nothing here retypes a
                        // model name or invents a context window.
                        models = FOUR_MODELS,
                        server = CustomEndpointOutcome.ServerKind.OLLAMA,
                    ),
                ),
                endpoints.saved,
            )
        }

    @Test
    fun `keeping the address saves exactly what was typed`() = runTest(dispatcher) {
        endpoints.answer = ollamaWithFour()
        val viewModel = adding()
        probe(viewModel, "http://192.168.1.9:11434")
        fill(viewModel, name = "The laptop")

        viewModel.onIntent(CustomEndpointIntent.KeepAddress, navigator)
        runCurrent()
        viewModel.onIntent(CustomEndpointIntent.Save, navigator)
        runCurrent()

        assertEquals("http://192.168.1.9:11434", endpoints.saved.single().address)
    }

    @Test
    fun `a server that listed nothing is saved with the empty roster that is the truth`() =
        runTest(dispatcher) {
            endpoints.answer = CustomEndpointOutcome.Reached(
                server = CustomEndpointOutcome.ServerKind.OPENAI_COMPATIBLE,
                modelCount = 0,
                models = emptyList(),
                provedBase = "http://192.168.1.9:11434/v1",
            )
            val viewModel = adding()
            probe(viewModel, "http://192.168.1.9:11434/v1")
            fill(viewModel, name = "The laptop")

            viewModel.onIntent(CustomEndpointIntent.Save, navigator)
            runCurrent()

            assertEquals(
                emptyList<CustomEndpointOutcome.Model>(),
                endpoints.saved.single().models,
            )
        }

    @Test
    fun `an answer about an address nobody is looking at any more is dropped`() =
        runTest(dispatcher) {
            endpoints.answer = ollamaWithFour()
            val viewModel = adding()
            viewModel.onIntent(
                CustomEndpointIntent.ChangeAddress("http://192.168.1.9:11434"),
                navigator,
            )
            runCurrent()
            viewModel.onIntent(CustomEndpointIntent.Probe, navigator)
            // The person keeps typing while the check is out.
            viewModel.onIntent(
                CustomEndpointIntent.ChangeAddress("http://192.168.1.9:11435"),
                navigator,
            )
            runCurrent()

            val state = viewModel.state.value
            assertNull(state.outcome)
            assertNull(state.proposal)
            assertFalse(state.probing)
        }

    @Test
    fun `a refused write keeps the page and says so`() = runTest(dispatcher) {
        endpoints.refusesSave = true
        val viewModel = adding()
        probe(viewModel, "http://192.168.1.9:11434/v1")
        fill(viewModel, name = "The laptop")

        viewModel.onIntent(CustomEndpointIntent.Save, navigator)
        runCurrent()

        assertTrue(viewModel.state.value.saveRefused)
        assertFalse(viewModel.state.value.saving)
        assertTrue(navigator.visited.isEmpty())
    }

    @Test
    fun `removing needs the confirmation, and then goes through the seam`() =
        runTest(dispatcher) {
            val viewModel = editing()

            viewModel.onIntent(CustomEndpointIntent.ConfirmDelete, navigator)
            runCurrent()
            assertTrue(endpoints.removed.isEmpty())

            viewModel.onIntent(CustomEndpointIntent.AskDelete, navigator)
            runCurrent()
            viewModel.onIntent(CustomEndpointIntent.ConfirmDelete, navigator)
            runCurrent()

            assertEquals(listOf("mine"), endpoints.removed)
            // And the key goes with it. The core dropped the credential on the
            // same command that dropped the provider, so a record left sealed
            // here is a key on somebody's phone for a provider no surface can
            // see, name or remove — which is what a phone was found holding on
            // 2026-09-03 (verification report 2.15).
            assertEquals(listOf("mine"), credentials.discarded)
            assertTrue(credentials.forgotten.isEmpty())
        }

    @Test
    fun `choosing a model is screen SCR-417 and is not written twice`() = runTest(dispatcher) {
        val viewModel = editing()

        viewModel.onIntent(CustomEndpointIntent.ChooseModel, navigator)
        runCurrent()

        assertEquals(listOf(TaffyDestination.ModelSelection("mine")), navigator.visited)
    }

    /**
     * Editing starts from what the browser registered rather than from an empty
     * field. Core API 3.18 puts the whole base on the roster row beside the
     * host, so the screen no longer has to ask a person to retype an address
     * TaffyGo already holds.
     */
    @Test
    fun `editing starts at the address the core registered`() = runTest(dispatcher) {
        val viewModel = editing()

        assertEquals("http://192.168.1.9:11434/v1", viewModel.state.value.address)
        assertEquals("192.168.1.9", viewModel.state.value.currentHost)
    }

    /**
     * Editing reuses the provider's own identity and mints none: the row being
     * probed is the row being edited, or the check would answer about a
     * provider that does not exist.
     */
    @Test
    fun `an edit probes under the provider it is editing`() = runTest(dispatcher) {
        val viewModel = editing()

        viewModel.onIntent(CustomEndpointIntent.Probe, navigator)
        runCurrent()

        assertEquals(
            listOf(FakeCustomEndpoints.Ask("http://192.168.1.9:11434/v1", "mine")),
            endpoints.probed,
        )
    }

    /**
     * The keyless server is the common case and it must stay exactly as it was.
     *
     * An empty field is null and never an empty handle. An empty handle is a
     * credential record that exists and resolves to nothing — the one state the
     * router refuses — so it would turn a plain Ollama that works into a
     * provider that cannot be reached at all.
     */
    @Test
    fun `an empty key field carries no credential rather than an empty one`() =
        runTest(dispatcher) {
            val viewModel = adding()
            probe(viewModel, "http://192.168.1.9:11434/v1")
            fill(viewModel, name = "The laptop")

            viewModel.onIntent(CustomEndpointIntent.Save, navigator)
            runCurrent()

            assertNull(endpoints.probed.single().credentialHandle)
            assertNull(endpoints.saved.single().credentialHandle)
            assertTrue(credentials.sealed.isEmpty())
        }

    /**
     * A key is sealed before each thing that carries it, and what travels is
     * the record's name.
     *
     * Twice rather than once on purpose: the field is what is true at the
     * moment of the action, so a key corrected after the check is the key the
     * write files. Nothing that crosses the seam is the key itself.
     */
    @Test
    fun `a key typed under advanced is sealed and its handle travels with both`() =
        runTest(dispatcher) {
            val viewModel = adding()
            key(viewModel, "sk-local-not-a-real-key")
            probe(viewModel, "http://192.168.1.9:11434/v1")
            fill(viewModel, name = "The laptop")

            viewModel.onIntent(CustomEndpointIntent.Save, navigator)
            runCurrent()

            assertEquals(
                listOf(
                    "192-168-1-9" to "sk-local-not-a-real-key",
                    "192-168-1-9" to "sk-local-not-a-real-key",
                ),
                credentials.sealed,
            )
            assertEquals("192-168-1-9", endpoints.probed.single().credentialHandle)
            assertEquals("192-168-1-9", endpoints.saved.single().credentialHandle)
        }

    /**
     * The verdict this screen could not act on before.
     *
     * A server that will list nothing without a key is now a question with an
     * answer: the advanced part opens by itself, the key goes in, and the same
     * address is checked again — carrying it this time.
     */
    @Test
    fun `a server that wants a key is asked again with one`() = runTest(dispatcher) {
        endpoints.answer =
            CustomEndpointOutcome.Refused(CustomEndpointOutcome.Problem.WANTS_A_CREDENTIAL)
        val viewModel = adding()
        probe(viewModel, "http://192.168.1.9:11434/v1")

        assertNull(endpoints.probed.single().credentialHandle)
        assertTrue(viewModel.state.value.advancedOpen)

        endpoints.answer = ollamaWithFour()
        key(viewModel, "sk-local-not-a-real-key")
        viewModel.onIntent(CustomEndpointIntent.Probe, navigator)
        runCurrent()

        assertEquals("192-168-1-9", endpoints.probed.last().credentialHandle)
        assertEquals(2, endpoints.probed.size)
    }

    /**
     * An edit that leaves the key field alone keeps the key.
     *
     * Nothing can show a stored key again, so an empty field on an edit means
     * "unchanged" and not "none". Sending no handle would be read by the core
     * as the person removing the credential, and an endpoint that had a key
     * would quietly start reaching their server with none.
     */
    @Test
    fun `an edit with an empty key field carries the key already stored`() =
        runTest(dispatcher) {
            credentials.hold("mine")
            val viewModel = editing()
            viewModel.onIntent(CustomEndpointIntent.Probe, navigator)
            runCurrent()

            viewModel.onIntent(CustomEndpointIntent.Save, navigator)
            runCurrent()

            assertTrue(viewModel.state.value.credentialHeld)
            assertEquals("mine", endpoints.probed.single().credentialHandle)
            assertEquals("mine", endpoints.saved.single().credentialHandle)
            assertTrue(credentials.sealed.isEmpty())
        }

    /**
     * A key this phone will not hold stops both consequential paths.
     *
     * Nothing is asked of the server and nothing is written: an endpoint filed
     * without the credential it was given would reach the person's own server
     * unauthenticated, which is worse than not being filed at all.
     */
    @Test
    fun `a key the store refuses stops the check and the write`() = runTest(dispatcher) {
        val viewModel = adding()
        key(viewModel, "sk-local-not-a-real-key")
        credentials.refuses = true

        probe(viewModel, "http://192.168.1.9:11434/v1")

        assertTrue(endpoints.probed.isEmpty())
        assertTrue(viewModel.state.value.keyStoreRefused)
        assertFalse(viewModel.state.value.probing)

        // And the write, reached by checking with the store working and then
        // losing it: the answer is on the page, the key is not stored, and the
        // save must not file the provider without it.
        credentials.refuses = false
        probe(viewModel, "http://192.168.1.9:11434/v1")
        fill(viewModel, name = "The laptop")
        credentials.refuses = true
        viewModel.onIntent(CustomEndpointIntent.Save, navigator)
        runCurrent()

        assertTrue(endpoints.saved.isEmpty())
        assertTrue(viewModel.state.value.keyStoreRefused)
        assertFalse(viewModel.state.value.saving)
    }

    @Test
    fun `the screen records itself as SCR-418`() = runTest(dispatcher) {
        adding().onShown()

        assertEquals(
            listOf(AnalyticsEvent.ScreenShown(TaffyDestination.CustomEndpointSetup().screenId)),
            analytics.recorded,
        )
    }

    private fun ollamaWithFour() = CustomEndpointOutcome.Reached(
        server = CustomEndpointOutcome.ServerKind.OLLAMA,
        modelCount = 4,
        models = FOUR_MODELS,
        // The prober reached the runtime on the bare origin and proved the
        // OpenAI-shaped API one segment below it. That difference is the whole
        // subject of this screen, and it is the prober's answer rather than
        // this screen's guess.
        provedBase = "http://192.168.1.9:11434/v1",
    )

    private fun TestScope.adding(): CustomEndpointViewModel {
        roster.publish()
        return started(SavedStateHandle())
    }

    private fun TestScope.editing(): CustomEndpointViewModel {
        roster.publish(
            providerRow(
                "mine",
                displayName = "The laptop",
                origin = RosterProviderOrigin.CUSTOM,
                catalogLayer = RosterCatalogLayer.USER_OVERRIDE,
            ),
        )
        endpoints.publishHost("mine", "192.168.1.9")
        endpoints.publishAddress("mine", "http://192.168.1.9:11434/v1")
        return started(SavedStateHandle(mapOf(TaffyDestination.ENDPOINT_ID to "mine")))
    }

    private fun TestScope.started(savedState: SavedStateHandle): CustomEndpointViewModel {
        val viewModel =
            CustomEndpointViewModel(roster, endpoints, credentials, analytics, savedState)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        return viewModel
    }

    private fun TestScope.probe(viewModel: CustomEndpointViewModel, address: String) {
        viewModel.onIntent(CustomEndpointIntent.ChangeAddress(address), navigator)
        runCurrent()
        viewModel.onIntent(CustomEndpointIntent.Probe, navigator)
        runCurrent()
    }

    private fun TestScope.fill(viewModel: CustomEndpointViewModel, name: String) {
        viewModel.onIntent(CustomEndpointIntent.ChangeName(name), navigator)
        runCurrent()
    }

    private fun TestScope.key(viewModel: CustomEndpointViewModel, typed: String) {
        viewModel.onIntent(CustomEndpointIntent.ChangeKey(typed), navigator)
        runCurrent()
    }

    private companion object {
        val FOUR_MODELS = listOf("llama3.1:8b", "qwen3:4b", "mistral:7b", "phi4:14b")
            .map(::fakeModel)
    }
}
