// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.model.ThinkingLevel
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
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
 * Screen SCR-417 states one whole choice per press, and draws only the echo.
 *
 * Two things this suite exists to hold: a model and a thinking level leave in
 * the same command, and the model marked in use is the roster's answer rather
 * than the press.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ModelSelectionViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val ladder = listOf(ThinkingLevel.OFF, ThinkingLevel.LOW, ThinkingLevel.HIGH)
    private val roster = FakeProviderRoster()
    private val credentials = FakeProviderCredentials()
    private val preferences = FakeProviderModelPreferences()
    private val navigator = RecordingProviderNavigator()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `choosing a rung names the model in the same command`() = runTest(dispatcher) {
        val viewModel = viewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        publish(standing = null)
        runCurrent()

        val row = viewModel.state.value.rows.single { it.modelId == "wide" }
        viewModel.onIntent(ModelSelectionIntent.ChooseThinking(row, ThinkingLevel.HIGH), navigator)
        runCurrent()

        // One command carrying both halves. A thinking write on its own would
        // have cleared the model, because the command is the whole choice.
        assertEquals(
            listOf(FakeProviderModelPreferences.Choice("alpha", "wide", ThinkingLevel.HIGH)),
            preferences.stated,
        )
    }

    @Test
    fun `choosing Auto sends no rung at all, and never OFF`() = runTest(dispatcher) {
        val viewModel = viewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        publish(standing = ThinkingLevel.HIGH)
        runCurrent()

        val row = viewModel.state.value.rows.single { it.modelId == "wide" }
        viewModel.onIntent(ModelSelectionIntent.ChooseThinking(row, null), navigator)
        runCurrent()

        // The difference this control exists for: Auto hands the core an
        // absence, where Off would hand it a rung meaning "do not think".
        val sent = preferences.stated.single()
        assertNull(sent.thinking)
        assertEquals("wide", sent.modelId)
    }

    @Test
    fun `choosing a model states the thinking that should stand with it`() = runTest(dispatcher) {
        val viewModel = viewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        publish(standing = ThinkingLevel.HIGH)
        runCurrent()

        val kept = viewModel.state.value.rows.single { it.modelId == "wide" }
        val dropped = viewModel.state.value.rows.single { it.modelId == "narrow" }
        viewModel.onIntent(ModelSelectionIntent.ChooseModel(kept), navigator)
        viewModel.onIntent(ModelSelectionIntent.ChooseModel(dropped), navigator)
        runCurrent()

        assertEquals(
            listOf(
                FakeProviderModelPreferences.Choice("alpha", "wide", ThinkingLevel.HIGH),
                // The narrow model cannot do HIGH, so the choice falls back to
                // Auto — stated rather than left out, because the command is
                // read as the whole choice either way.
                FakeProviderModelPreferences.Choice("alpha", "narrow", null),
            ),
            preferences.stated,
        )
    }

    @Test
    fun `a refused choice leaves the screen showing what still stands`() = runTest(dispatcher) {
        preferences.refuses = true
        val viewModel = viewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        publish(standing = null)
        runCurrent()

        val row = viewModel.state.value.rows.single { it.modelId == "narrow" }
        viewModel.onIntent(ModelSelectionIntent.ChooseModel(row), navigator)
        runCurrent()

        // Nothing was published, so nothing changed: the model marked in use is
        // the roster echo and never the press.
        assertEquals("wide", viewModel.state.value.rows.single { it.selected }.modelId)
        assertTrue(preferences.stated.isEmpty())
        assertTrue(viewModel.state.value.rows.none { it.asking })
    }

    @Test
    fun `a republished roster is what changes the screen`() = runTest(dispatcher) {
        val viewModel = viewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        assertEquals(ModelSelectionUiState.Status.LOADING, viewModel.state.value.status)

        publish(standing = ThinkingLevel.LOW)
        runCurrent()

        assertEquals(ModelSelectionUiState.Status.READY, viewModel.state.value.status)
        assertEquals(
            ThinkingLevel.LOW,
            viewModel.state.value.rows.single { it.selected }.thinking.chosen,
        )
    }

    private fun viewModel() = ModelSelectionViewModel(
        roster = roster,
        credentials = credentials,
        preferences = preferences,
        analytics = RecordingProviderAnalytics(),
        savedState = SavedStateHandle(mapOf(TaffyDestination.PROVIDER_ID to "alpha")),
    )

    private fun publish(standing: ThinkingLevel?) {
        credentials.hold("alpha")
        roster.publish(
            providerRow(
                "alpha",
                stored = storedCredential(),
                selectedModelId = "wide",
                thinking = standing,
            ),
        )
        roster.publishModels(
            mapOf(
                "alpha" to listOf(
                    providerModel("alpha", "wide", thinkingLevels = ladder),
                    providerModel(
                        "alpha",
                        "narrow",
                        thinkingLevels = listOf(ThinkingLevel.OFF, ThinkingLevel.LOW),
                    ),
                ),
            ),
        )
    }
}
