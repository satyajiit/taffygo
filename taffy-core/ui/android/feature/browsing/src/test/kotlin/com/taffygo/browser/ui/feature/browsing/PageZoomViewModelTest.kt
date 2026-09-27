// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.Dispatchers
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

class PageZoomViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `zoom intents reach only the selected-page zoom port`() = runTest(dispatcher) {
        val zoom = RecordingPageZoomRepository()
        val viewModel = browserMainViewModel(pageZoom = zoom)

        val navigator = NoNavigation()
        viewModel.onIntent(BrowserMainIntent.ZoomPageOut, navigator)
        viewModel.onIntent(BrowserMainIntent.ResetPageZoom, navigator)
        viewModel.onIntent(BrowserMainIntent.ZoomPageIn, navigator)
        runCurrent()

        assertEquals(listOf("out", "reset", "in"), zoom.actions)
    }

    private class RecordingPageZoomRepository : PageZoomRepository {
        override val state: StateFlow<PageZoomState> = MutableStateFlow(
            PageZoomState(
                available = true,
                canZoomOut = true,
                canZoomIn = true,
            ),
        )
        val actions = mutableListOf<String>()

        override suspend fun zoomIn() {
            actions += "in"
        }

        override suspend fun zoomOut() {
            actions += "out"
        }

        override suspend fun reset() {
            actions += "reset"
        }
    }
}
