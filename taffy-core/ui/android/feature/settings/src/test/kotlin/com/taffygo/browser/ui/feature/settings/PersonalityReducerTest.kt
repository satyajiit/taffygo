// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
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

/** Screen SCR-603's projection and honest save. */
@OptIn(ExperimentalCoroutinesApi::class)
class PersonalityReducerTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `Empty starts on the careful researcher`() {
        val state = projectPersonality(EmptyPersonalityRepository().snapshot.value, false)
        assertEquals(PersonalityRepository.Availability.READY, state.availability)
        assertEquals(PersonalityRepository.Preset.CAREFUL_RESEARCHER, state.selected)
        assertEquals(PersonalityRepository.Preset.entries, state.presets)
        assertEquals(false, state.choiceNotSaved)
    }

    @Test
    fun `choosing a preset on Empty selects it`() = runTest(dispatcher) {
        val personality = EmptyPersonalityRepository()
        val viewModel = PersonalityViewModel(personality, NoAnalytics())
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(
            PersonalityIntent.ChoosePreset(PersonalityRepository.Preset.QUICK_SHOPPER),
            RecordingNavigator(),
        )
        runCurrent()

        assertEquals(PersonalityRepository.Preset.QUICK_SHOPPER, viewModel.state.value.selected)
        assertEquals(
            PersonalityRepository.Preset.QUICK_SHOPPER.scales(),
            personality.snapshot.value.scales,
        )
        assertEquals(false, viewModel.state.value.choiceNotSaved)
    }

    @Test
    fun `choosing a preset when unavailable does not claim it saved`() = runTest(dispatcher) {
        val personality = UnavailablePersonalityRepository()
        val viewModel = PersonalityViewModel(personality, NoAnalytics())
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(
            PersonalityIntent.ChoosePreset(PersonalityRepository.Preset.TRIP_PLANNER),
            RecordingNavigator(),
        )
        runCurrent()

        assertNull(personality.snapshot.value.selected)
        assertTrue(viewModel.state.value.choiceNotSaved)
    }

    @Test
    fun `tuning opens the scales screen`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = PersonalityViewModel(EmptyPersonalityRepository(), NoAnalytics())
        viewModel.onIntent(PersonalityIntent.OpenTuning, navigator)
        assertEquals(listOf(TaffyDestination.PersonalityTuning), navigator.opened)
    }

    private class RecordingNavigator : TaffyNavigator {
        val opened = mutableListOf<TaffyDestination>()
        override fun goTo(destination: TaffyDestination) {
            opened += destination
        }
        override fun replaceCurrent(destination: TaffyDestination) = Unit
        override fun goBack(): Boolean = false
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}
