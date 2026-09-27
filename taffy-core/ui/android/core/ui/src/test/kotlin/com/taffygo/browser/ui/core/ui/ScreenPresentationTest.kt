// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Which screen owns the frame, and which overlay stands over it. */
class ScreenPresentationTest {

    private val ask = TaffyDestination.AssistantBar(question = "is this fee refundable?")

    @Test
    fun `an ask over the browsing surface keeps the surface as the base`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain).push(ask)

        val presentation = stack.presentation()

        assertEquals(TaffyDestination.BrowserMain, presentation.base)
        assertEquals(ask, presentation.overlay)
    }

    @Test
    fun `a screen on top is the base and there is no overlay`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain).push(TaffyDestination.AddressBar)

        assertEquals(TaffyDestination.AddressBar, stack.presentation().base)
        assertNull(stack.presentation().overlay)
    }

    @Test
    fun `the base is the nearest screen beneath the overlay`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain)
            .push(TaffyDestination.SettingsHome)
            .push(ask)

        assertEquals(TaffyDestination.SettingsHome, stack.presentation().base)
        assertEquals(ask, stack.presentation().overlay)
    }

    @Test
    fun `a screen pushed over an overlay takes the frame and the overlay waits beneath`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain)
            .push(ask)
            .push(TaffyDestination.TaskView)

        assertEquals(TaffyDestination.TaskView, stack.presentation().base)
        assertNull(stack.presentation().overlay)
        assertEquals(ask, stack.pop().presentation().overlay)
    }

    @Test
    fun `a stack of nothing but overlays stands on the start destination`() {
        val stack = BackStack(listOf(ask))

        assertEquals(TaffyDestination.START, stack.presentation().base)
        assertEquals(ask, stack.presentation().overlay)
    }
}
