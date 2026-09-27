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
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

/**
 * The two rows decision 0206 puts on About, and the one call site each.
 *
 * The licences row reaches the browser through the entry point that takes no address, and never
 * through `openTab`, because the address it opens is an engine page every other entry point
 * refuses. The source row is an ordinary address and goes the ordinary way.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class AboutViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `the licences row asks for the notice and names no address`() = runTest(dispatcher) {
        val browser = RecordingBrowser(noticeOpens = true)
        val navigator = RecordingNavigator()
        val viewModel = AboutViewModel(UnavailableAboutRepository(), browser, NoAnalytics())

        viewModel.onIntent(AboutIntent.OpenLicences, navigator)
        runCurrent()

        assertEquals(1, browser.noticeRequests)
        assertEquals(emptyList<String>(), browser.openedAddresses)
        assertEquals(listOf<TaffyDestination>(TaffyDestination.BrowserMain), navigator.opened)
    }

    @Test
    fun `a notice the browser could not open leaves About showing`() = runTest(dispatcher) {
        val browser = RecordingBrowser(noticeOpens = false)
        val navigator = RecordingNavigator()
        val viewModel = AboutViewModel(UnavailableAboutRepository(), browser, NoAnalytics())

        viewModel.onIntent(AboutIntent.OpenLicences, navigator)
        runCurrent()

        assertEquals(1, browser.noticeRequests)
        assertEquals(emptyList<TaffyDestination>(), navigator.opened)
    }

    @Test
    fun `the source row opens the public repository in a tab`() = runTest(dispatcher) {
        val browser = RecordingBrowser(noticeOpens = true)
        val navigator = RecordingNavigator()
        val viewModel = AboutViewModel(UnavailableAboutRepository(), browser, NoAnalytics())

        viewModel.onIntent(AboutIntent.OpenSource, navigator)
        runCurrent()

        assertEquals(listOf("https://github.com/satyajiit/taffygo"), browser.openedAddresses)
        assertEquals(0, browser.noticeRequests)
        assertEquals(listOf<TaffyDestination>(TaffyDestination.BrowserMain), navigator.opened)
    }

    private class RecordingBrowser(private val noticeOpens: Boolean) : BrowserRepository {
        val openedAddresses = mutableListOf<String>()
        var noticeRequests = 0

        override suspend fun openAttributionNotice(): Boolean {
            noticeRequests += 1
            return noticeOpens
        }

        override suspend fun openTab(host: String, isPrivate: Boolean): TabId {
            openedAddresses += host
            return TabId("tab-1")
        }

        override val filtering: StateFlow<FilteringSettings> = MutableStateFlow(FilteringSettings())
        override suspend fun setFilteringEnabled(enabled: Boolean) = Unit
        override suspend fun setSiteFilteringException(
            host: String,
            allow: Boolean,
            plane: SiteFilteringPlane,
        ): Boolean = false
        override suspend fun flushFilteringCounts() = Unit
        override val tabs: StateFlow<List<Tab>> = MutableStateFlow(emptyList())
        override val navigation: StateFlow<NavigationState> =
            MutableStateFlow(NavigationState(host = "", title = ""))
        override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
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
        override suspend fun goBack(): Boolean = false
        override suspend fun goForward(): Boolean = false
        override suspend fun reload() = Unit
        override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) = false
        override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit
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
