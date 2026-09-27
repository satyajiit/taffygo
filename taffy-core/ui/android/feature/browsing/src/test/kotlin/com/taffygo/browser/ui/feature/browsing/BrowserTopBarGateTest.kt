// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.model.TaffyPartId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Which content screen SCR-101 draws a top bar over, decided without a device.
 *
 * The gate is the one the address strip carried when the pill lived at the
 * bottom, and moving the pill must not have widened it: the start page centres
 * its own address box in its body, so a bar above it would be a second control
 * asking the same question, and the preparing state offers no address entry at
 * all. Those two states are the new tab, and the new tab does not change.
 *
 * It is asserted here rather than only in the instrumented semantics suite
 * because it is a plain function of the state and because a device is not
 * available to everyone who might widen it.
 */
class BrowserTopBarGateTest {

    private val ready = StartPageGate(
        ready = true,
        partId = TaffyPartId("python-stdlib"),
        downloadedBytes = 1,
        totalBytes = 1,
    )

    @Test
    fun `a tab that has been nowhere draws the start page and no top bar`() {
        val state = BrowserMainUiState(hasBeenNowhere = true, startPageGate = ready)

        assertEquals(BrowserContent.START, state.content)
        assertFalse(state.hasTopBar())
        assertFalse(state.showsTopBar())
    }

    @Test
    fun `a tab still waiting for the page tools draws no top bar either`() {
        val state = BrowserMainUiState(hasBeenNowhere = true)

        assertEquals(BrowserContent.PREPARING, state.content)
        assertFalse(state.hasTopBar())
        assertFalse(state.showsTopBar())
    }

    @Test
    fun `a page carries the top bar`() {
        val state = BrowserMainUiState(host = "docs.example.test", title = "Retention policy")

        assertEquals(BrowserContent.PAGE, state.content)
        assertTrue(state.hasTopBar())
        assertTrue(state.showsTopBar())
    }

    /**
     * A failure keeps it: the address is the one thing still true about where
     * the person was sent, and reload lives in the overflow beside it.
     */
    @Test
    fun `a failed page keeps the top bar`() {
        val state = BrowserMainUiState(
            host = "gone.example.test",
            failure = PageLoadFailure.NAME_NOT_RESOLVED,
        )

        assertEquals(BrowserContent.FAILED, state.content)
        assertTrue(state.hasTopBar())
        assertTrue(state.showsTopBar())
    }

    /**
     * Scrolling takes the bar off the screen without taking it out of the
     * layout: [BrowserMainUiState.hasTopBar] stays true, which is what keeps
     * the status-bar inset where it was and the page the size it was.
     */
    @Test
    fun `a scrolled-away bar is hidden but still this content's bar`() {
        val state = BrowserMainUiState(
            host = "docs.example.test",
            title = "Retention policy",
            topBarVisible = false,
        )

        assertTrue(state.hasTopBar())
        assertFalse(state.showsTopBar())
    }
}
