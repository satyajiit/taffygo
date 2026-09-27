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
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class ClearBrowsingDataViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `a partial or refused deletion stays on screen and says it failed`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = ClearBrowsingDataViewModel(ResultRepository(false), NoAnalytics())

        viewModel.onIntent(ClearBrowsingDataIntent.Confirm, navigator)
        viewModel.onIntent(ClearBrowsingDataIntent.Submit, navigator)
        runCurrent()

        assertTrue(viewModel.state.value.failed)
        assertFalse(viewModel.state.value.submitting)
        assertFalse(navigator.wentBack)
    }

    @Test
    fun `only a complete deletion closes the screen`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = ClearBrowsingDataViewModel(ResultRepository(true), NoAnalytics())

        viewModel.onIntent(ClearBrowsingDataIntent.Confirm, navigator)
        viewModel.onIntent(ClearBrowsingDataIntent.Submit, navigator)
        runCurrent()

        assertFalse(viewModel.state.value.failed)
        assertTrue(navigator.wentBack)
    }

    private class ResultRepository(private val result: Boolean) : ClearDataRepository {
        override val available: Boolean = true
        override val supportedClasses: Set<ClearBrowsingDataUiState.DataClass> =
            ClearBrowsingDataUiState.DataClass.entries.toSet()

        override suspend fun clear(
            range: ClearBrowsingDataUiState.Range,
            classes: Set<ClearBrowsingDataUiState.DataClass>,
        ): Boolean = result
    }

    private class RecordingNavigator : TaffyNavigator {
        var wentBack = false

        override fun goTo(destination: TaffyDestination) = Unit
        override fun replaceCurrent(destination: TaffyDestination) = Unit
        override fun goBack(): Boolean {
            wentBack = true
            return true
        }
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}
