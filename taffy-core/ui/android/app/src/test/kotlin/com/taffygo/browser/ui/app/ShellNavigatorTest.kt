// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyDestinationGroups
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The one place a destination becomes a screen change.
 *
 * The shell is the only module that knows more than one feature, so the rule
 * that keeps cross-feature navigation from becoming a cross-feature dependency
 * is tested here: a feature hands over a destination, and nothing else.
 */
class ShellNavigatorTest {

    private var stack = BackStack.start()
    private val navigator = ShellNavigator(current = { stack }, update = { stack = it })

    @Test
    fun `going somewhere puts it on top and keeps what was beneath`() {
        navigator.goTo(TaffyDestination.WorkspaceList)
        navigator.goTo(TaffyDestination.SettingsHome)

        assertEquals(TaffyDestination.SettingsHome, stack.current)
        assertEquals(3, stack.entries.size)
        assertEquals(TaffyDestination.START, stack.entries.first())
    }

    @Test
    fun `going back returns true and returns to where the user was`() {
        navigator.goTo(TaffyDestination.WorkspaceList)

        assertTrue(navigator.goBack())
        assertEquals(TaffyDestination.START, stack.current)
    }

    @Test
    fun `leaving to background asks the host and does not pop the stack`() {
        var left = 0
        val leaving = ShellNavigator(
            current = { stack },
            update = { stack = it },
            onLeaveToBackground = {
                left += 1
                true
            },
        )
        leaving.goTo(TaffyDestination.WorkspaceList)

        assertTrue(leaving.leaveToBackground())
        assertEquals(1, left)
        assertEquals(TaffyDestination.WorkspaceList, stack.current)
    }

    @Test
    fun `going back from the start destination refuses rather than emptying the stack`() {
        assertFalse(navigator.goBack())
        assertEquals(1, stack.entries.size)
        assertEquals(TaffyDestination.START, stack.current)
    }

    @Test
    fun `going home returns to the bottom however deep the stack got`() {
        navigator.goTo(TaffyDestination.WorkspaceList)
        navigator.goTo(TaffyDestination.WorkspaceDetail("ws_0"))
        navigator.goTo(TaffyDestination.SourceViewer("ws_0", "src_0"))

        navigator.goHome()

        assertEquals(BackStack.start(), stack)
    }

    @Test
    fun `opening the destination already on top does not stack a second copy`() {
        navigator.goTo(TaffyDestination.SettingsHome)
        navigator.goTo(TaffyDestination.SettingsHome)

        assertEquals(2, stack.entries.size)
    }

    /**
     * The shell's half of the browser's back button.
     *
     * A feature says "the box is finished with, show the page"; this is the
     * only place that turns it into a stack that did not grow. The rule itself
     * is [BackStack.replaceCurrent]'s and is tested there — what is asserted
     * here is that the contract a feature holds reaches it.
     */
    @Test
    fun `replacing the current screen returns to the one beneath instead of stacking it`() {
        stack = BackStack.start(TaffyDestination.BrowserMain)
        navigator.goTo(TaffyDestination.AddressBar)

        navigator.replaceCurrent(TaffyDestination.BrowserMain)

        assertEquals(listOf(TaffyDestination.BrowserMain), stack.entries)
        assertFalse(navigator.goBack())
    }

    @Test
    fun `popWhile leaves a whole settings group in one step`() {
        navigator.goTo(TaffyDestination.SettingsHome)
        navigator.goTo(TaffyDestination.Appearance)

        navigator.popWhile(TaffyDestinationGroups::isSettings)

        assertEquals(TaffyDestination.START, stack.current)
    }

    @Test
    fun `restarting replaces the stack so back cannot return to setup`() {
        stack = BackStack.start(TaffyDestination.SettingsHome)
        navigator.goTo(TaffyDestination.Appearance)

        navigator.restart(TaffyDestination.BrowserMain)

        assertEquals(BackStack.start(TaffyDestination.BrowserMain), stack)
        assertFalse(navigator.goBack())
    }

    @Test
    fun `navigation motion reverses only when returning to a stack prefix`() {
        val root = BackStack.start(TaffyDestination.OnboardingWelcome)
        val pushed = root.push(TaffyDestination.MeetTaffy)

        assertFalse(isBackNavigation(root, pushed))
        assertTrue(isBackNavigation(pushed, root))
        assertFalse(
            isBackNavigation(
                pushed,
                pushed.replaceAll(TaffyDestination.BrowserMain),
            ),
        )
    }

    @Test
    fun `a destination with arguments round trips through its route`() {
        navigator.goTo(TaffyDestination.WorkspaceDetail("ws_0"))

        val restored = TaffyDestination.fromRoute(stack.current.route)

        assertEquals(stack.current, restored)
    }

    @Test
    fun `a sheet over the browsing surface still counts as the browsing surface`() {
        val browsing = BackStack.start(TaffyDestination.BrowserMain)
        val asking = browsing.push(TaffyDestination.AssistantBar(question = "is this fee refundable?"))
        val settings = BackStack.start(TaffyDestination.SettingsHome)

        // Opening the sheet, and opening the task view from it, both keep the
        // page on one side of the transition, so neither may slide it.
        assertTrue(movesTheBrowsingSurface(browsing, asking))
        assertTrue(movesTheBrowsingSurface(asking, asking.push(TaffyDestination.TaskView)))
        assertFalse(
            movesTheBrowsingSurface(
                settings,
                settings.push(TaffyDestination.AssistantBar(question = "why")),
            ),
        )
    }
}
