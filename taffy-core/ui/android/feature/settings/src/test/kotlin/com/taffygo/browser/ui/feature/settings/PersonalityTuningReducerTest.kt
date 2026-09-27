// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
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
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Screen SCR-604's scales never grant a permission. */
@OptIn(ExperimentalCoroutinesApi::class)
class PersonalityTuningReducerTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `a scale change stays on its own axis and clamps`() {
        val start = PersonalityRepository.Scales(pace = 0, length = 1, checkIn = 0)
        assertEquals(
            PersonalityRepository.Scales(pace = 2, length = 1, checkIn = 0),
            applyTuningScale(start, PersonalityTuningIntent.Axis.PACE, 2),
        )
        assertEquals(
            PersonalityRepository.Scales(pace = 0, length = 2, checkIn = 0),
            applyTuningScale(start, PersonalityTuningIntent.Axis.LENGTH, 99),
        )
        assertEquals(
            PersonalityRepository.Scales(pace = 0, length = 1, checkIn = 0),
            applyTuningScale(start, PersonalityTuningIntent.Axis.CHECK_IN, -4),
        )
    }

    @Test
    fun `Empty records a scale without changing what Taffy may do`() = runTest(dispatcher) {
        val personality = EmptyPersonalityRepository()
        val viewModel = PersonalityTuningViewModel(personality, NoAnalytics())
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(
            PersonalityTuningIntent.SetScale(PersonalityTuningIntent.Axis.LENGTH, 2),
        )
        runCurrent()

        assertEquals(2, viewModel.state.value.scales.length)
        assertEquals(0, viewModel.state.value.scales.pace)
        assertEquals(false, viewModel.state.value.notSaved)
    }

    @Test
    fun `unavailable scale choice captions that it was not saved`() = runTest(dispatcher) {
        val personality = UnavailablePersonalityRepository()
        val viewModel = PersonalityTuningViewModel(personality, NoAnalytics())
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(
            PersonalityTuningIntent.SetScale(PersonalityTuningIntent.Axis.PACE, 2),
        )
        runCurrent()

        assertEquals(0, personality.snapshot.value.scales.pace)
        assertTrue(viewModel.state.value.notSaved)
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}
