// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
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

/**
 * The site sheet's blocking switch, from intent to seam.
 *
 * Two things had no test at all. The switch is labelled *block* while the seam
 * records an *allowance*, so the command inverts — and an inversion nothing
 * checks is one refactor from being silently dropped. And the seam's refusal was
 * discarded, so a switch could appear to move while nothing had been recorded.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class BrowserMainSiteBlockingTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `turning the switch off allows the site on the selected tab's plane`() =
        runTest(dispatcher) {
            val browser = FakeBrowser(host = "news.example.test")
            val viewModel = browserMainViewModel(browser)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(BrowserMainIntent.SetSiteBlocking(false), NoNavigation())
            runCurrent()

            // Blocked off means allowed on: the switch is about blocking and
            // the seam records the exception.
            assertEquals(
                listOf(
                    Triple("news.example.test", true, SiteFilteringPlane.SELECTED_TAB),
                ),
                browser.siteExceptions,
            )
        }

    @Test
    fun `turning the switch on removes the allowance, still on the tab's plane`() =
        runTest(dispatcher) {
            val browser = FakeBrowser(host = "news.example.test")
            val viewModel = browserMainViewModel(browser)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(BrowserMainIntent.SetSiteBlocking(true), NoNavigation())
            runCurrent()

            assertEquals(
                listOf(
                    Triple("news.example.test", false, SiteFilteringPlane.SELECTED_TAB),
                ),
                browser.siteExceptions,
            )
        }

    @Test
    fun `a refusal is shown rather than discarded`() = runTest(dispatcher) {
        val browser = FakeBrowser(host = "news.example.test")
        browser.recordsSiteExceptions = false
        val viewModel = browserMainViewModel(browser)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.SetSiteBlocking(false), NoNavigation())
        runCurrent()

        assertEquals(
            SiteFilteringUiState.ActionProgress.FAILED,
            viewModel.state.value.siteFiltering.siteBlocking,
        )
    }

    @Test
    fun `a recorded change says nothing more than the switch already says`() =
        runTest(dispatcher) {
            val browser = FakeBrowser(host = "news.example.test")
            val viewModel = browserMainViewModel(browser)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(BrowserMainIntent.SetSiteBlocking(false), NoNavigation())
            runCurrent()

            assertEquals(
                SiteFilteringUiState.ActionProgress.SUCCEEDED,
                viewModel.state.value.siteFiltering.siteBlocking,
            )
        }

    @Test
    fun `a tab that has been nowhere names no host and records nothing`() =
        runTest(dispatcher) {
            val browser = FakeBrowser(host = "")
            val viewModel = browserMainViewModel(browser)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(BrowserMainIntent.SetSiteBlocking(false), NoNavigation())
            runCurrent()

            assertTrue(browser.siteExceptions.isEmpty())
            assertEquals(
                SiteFilteringUiState.ActionProgress.IDLE,
                viewModel.state.value.siteFiltering.siteBlocking,
            )
        }
}
