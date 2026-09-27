// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class FilteringSettingsViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `the toggle reaches the browser and comes back as state`() = runTest(dispatcher) {
        val browser = FakeBrowser()
        val viewModel = FilteringSettingsViewModel(
            browser,
            UnavailableBlockingWeekRepository(),
            NoAnalytics(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        assertTrue(viewModel.state.value.enabled)

        viewModel.onIntent(FilteringSettingsIntent.SetEnabled(false))
        runCurrent()

        assertFalse(viewModel.state.value.enabled)
    }

    @Test
    fun `removing an exception turns blocking back on for that site`() = runTest(dispatcher) {
        val browser = FakeBrowser(
            initial = FilteringSettings(exceptionHosts = listOf("news.example.test")),
        )
        val viewModel = FilteringSettingsViewModel(
            browser,
            UnavailableBlockingWeekRepository(),
            NoAnalytics(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(FilteringSettingsIntent.RemoveException("news.example.test"))
        runCurrent()

        assertEquals(emptyList<String>(), viewModel.state.value.exceptionHosts)
    }

    /**
     * The browser seam with only its filtering half modelled: the commands
     * mutate the held configuration the way the real seam's preference write
     * comes back around, and everything else is the disconnected answer.
     */
    private class FakeBrowser(
        initial: FilteringSettings = FilteringSettings(),
    ) : BrowserRepository {
        private val filteringState = MutableStateFlow(initial)

        override val filtering: StateFlow<FilteringSettings> = filteringState.asStateFlow()

        override suspend fun setFilteringEnabled(enabled: Boolean) {
            filteringState.value = filteringState.value.copy(enabled = enabled)
        }

        override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean {
            val current = filteringState.value
            filteringState.value = current.copy(
                exceptionHosts = if (allow) {
                    (current.exceptionHosts + host).distinct()
                } else {
                    current.exceptionHosts - host
                },
            )
            return true
        }

        override suspend fun flushFilteringCounts() = Unit

        override val tabs: StateFlow<List<Tab>> = MutableStateFlow(emptyList())
        override val navigation: StateFlow<NavigationState> =
            MutableStateFlow(NavigationState(host = "", title = ""))
        override val pageAppearance: StateFlow<PageAppearance> =
            MutableStateFlow(PageAppearance())
        override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())
        override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())
        override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())
        override val notice: StateFlow<BrowserNotice?> = MutableStateFlow(null)

        override fun resolve(input: String): AddressBarInterpretation =
            AddressBarInterpretation.GoTo(input, input)

        override fun suggestions(input: String): List<Suggestion> = emptyList()

        override suspend fun commit(interpretation: AddressBarInterpretation) = Unit

        override fun dismissNotice() = Unit

        override suspend fun selectTab(id: TabId) = Unit

        override suspend fun closeTab(id: TabId) = Unit

        override suspend fun openTab(host: String, isPrivate: Boolean): TabId = TabId("")

        override suspend fun goBack(): Boolean = false

        override suspend fun goForward(): Boolean = false

        override suspend fun reload() = Unit

        override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) = false

        override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}
