// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.SearchEngine
import com.taffygo.browser.ui.core.browser.SearchEngineId
import com.taffygo.browser.ui.core.browser.SearchEngineRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
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

@OptIn(ExperimentalCoroutinesApi::class)
class GeneralViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `selection write and readback failures stay visible in the picker`() = runTest(dispatcher) {
        val failures = mapOf(
            GeneralSettingsRepository.DownloadLocationChoice.SELECTION_UNAVAILABLE to
                DownloadLocationFailure.SELECTION_UNAVAILABLE,
            GeneralSettingsRepository.DownloadLocationChoice.WRITE_FAILED to
                DownloadLocationFailure.WRITE_FAILED,
            GeneralSettingsRepository.DownloadLocationChoice.READBACK_FAILED to
                DownloadLocationFailure.READBACK_FAILED,
        )
        for ((result, expected) in failures) {
            val viewModel = GeneralViewModel(FakeGeneral(result), FakeSearch(), NoAnalytics())
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(GeneralIntent.ChooseDownloadLocation, NoNavigator())
            runCurrent()
            viewModel.onIntent(GeneralIntent.SelectDownloadLocation("device"), NoNavigator())
            runCurrent()

            assertEquals(expected, viewModel.state.value.downloadLocationFailure)
            assertTrue(viewModel.state.value.pickingDownloadLocation)
        }
    }

    @Test
    fun `a verified save closes the picker without an error`() = runTest(dispatcher) {
        val viewModel = GeneralViewModel(
            FakeGeneral(GeneralSettingsRepository.DownloadLocationChoice.SAVED),
            FakeSearch(),
            NoAnalytics(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(GeneralIntent.ChooseDownloadLocation, NoNavigator())
        runCurrent()
        viewModel.onIntent(GeneralIntent.SelectDownloadLocation("device"), NoNavigator())
        runCurrent()

        assertEquals(null, viewModel.state.value.downloadLocationFailure)
        assertEquals(false, viewModel.state.value.pickingDownloadLocation)
    }

    private class FakeGeneral(
        private val result: GeneralSettingsRepository.DownloadLocationChoice,
    ) : GeneralSettingsRepository {
        override val snapshot: StateFlow<GeneralSettingsRepository.Snapshot> = MutableStateFlow(
            GeneralSettingsRepository.Snapshot(
                downloadLocationsLoading = false,
                downloadLocations = listOf(
                    GeneralSettingsRepository.DownloadLocation(
                        "device",
                        GeneralSettingsRepository.DownloadLocation.Kind.DEVICE,
                    ),
                ),
            ),
        )

        override fun refreshDownloadLocations() = Unit
        override suspend fun chooseDownloadLocation(id: String) = result
    }

    private class FakeSearch : SearchEngineRepository {
        override val selectedId: StateFlow<SearchEngineId> = MutableStateFlow(SearchEngineId.DEFAULT)
        override fun listed(regionCode: String): List<SearchEngine> = emptyList()
        override fun searchUrl(query: String): String? = null
        override fun taskSearchUrl(query: String): String? = null
        override suspend fun select(id: SearchEngineId) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private class NoNavigator : TaffyNavigator {
        override fun goTo(destination: TaffyDestination) = Unit
        override fun replaceCurrent(destination: TaffyDestination) = Unit
        override fun goBack(): Boolean = true
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }
}
