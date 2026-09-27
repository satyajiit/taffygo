// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The navigation rules, as unit tests. Navigation is a reducer like everything
 * else in the UI host, which is what makes these tests possible at all.
 */
class BackStackTest {

    @Test
    fun `a fresh stack holds the start destination and nothing else`() {
        val stack = BackStack.start()

        assertEquals(listOf(TaffyDestination.START), stack.entries)
        assertFalse(stack.canGoBack)
    }

    @Test
    fun `pushing moves forward and going back returns`() {
        val pushed = BackStack.start().push(TaffyDestination.Downloads)

        assertEquals(TaffyDestination.Downloads, pushed.current)
        assertTrue(pushed.canGoBack)
        assertEquals(TaffyDestination.START, pushed.pop().current)
    }

    @Test
    fun `pushing the destination already on top does nothing`() {
        val once = BackStack.start().push(TaffyDestination.Downloads)
        val twice = once.push(TaffyDestination.Downloads)

        assertEquals(once, twice)
        assertEquals(2, twice.entries.size)
    }

    @Test
    fun `two destinations of the same screen with different arguments are different entries`() {
        val stack = BackStack.start()
            .push(TaffyDestination.WorkspaceDetail("ws_one"))
            .push(TaffyDestination.WorkspaceDetail("ws_two"))

        assertEquals(3, stack.entries.size)
        assertEquals(TaffyDestination.WorkspaceDetail("ws_two"), stack.current)
    }

    @Test
    fun `going back from the bottom stays put`() {
        val stack = BackStack.start()

        assertEquals(stack, stack.pop())
    }

    @Test
    fun `replacing the current entry leaves it behind rather than under`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain)
            .push(TaffyDestination.AddressBar)
        // Held rather than written twice: an ask is a new screen each time it
        // is opened, so two of them are two entries and not one.
        val preview = TaffyDestination.AssistantBar(question = "compare two phones")

        val replaced = stack.replaceCurrent(preview)

        assertEquals(listOf(TaffyDestination.BrowserMain, preview), replaced.entries)
    }

    /**
     * The defect the system back button showed, stated as arithmetic.
     *
     * Each committed address opened SCR-103 and then *pushed* SCR-101 on top of
     * it, so a stack of one became a stack of three, then five, then seven. The
     * pair is what back walked into: a browser that had been used once needed
     * four presses to leave and offered an address bar on the way out.
     */
    @Test
    fun `going to and from the address bar leaves the stack the size it was`() {
        var stack = BackStack.start(TaffyDestination.BrowserMain)

        repeat(5) {
            stack = stack.push(TaffyDestination.AddressBar)
            stack = stack.replaceCurrent(TaffyDestination.BrowserMain)
        }

        assertEquals(listOf(TaffyDestination.BrowserMain), stack.entries)
        assertFalse(stack.canGoBack)
    }

    @Test
    fun `returning to a destination already held drops everything above it`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain)
            .push(TaffyDestination.TabSwitcher)
            .push(TaffyDestination.NewTab)
            .push(TaffyDestination.AddressBar)

        val replaced = stack.replaceCurrent(TaffyDestination.BrowserMain)

        assertEquals(listOf(TaffyDestination.BrowserMain), replaced.entries)
    }

    @Test
    fun `replacing the only entry opens above it rather than emptying the stack`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain)

        val replaced = stack.replaceCurrent(TaffyDestination.AddressBar)

        assertEquals(
            listOf(TaffyDestination.BrowserMain, TaffyDestination.AddressBar),
            replaced.entries,
        )
    }

    @Test
    fun `replacing keeps the arguments a destination is distinguished by`() {
        val stack = BackStack.start()
            .push(TaffyDestination.WorkspaceDetail("ws_one"))
            .push(TaffyDestination.SourceViewer("ws_one", "src_0"))

        val replaced = stack.replaceCurrent(TaffyDestination.WorkspaceDetail("ws_two"))

        // `ws_two` is not the entry beneath, so it is opened in the source
        // viewer's place rather than being mistaken for `ws_one`.
        assertEquals(
            listOf(
                TaffyDestination.START,
                TaffyDestination.WorkspaceDetail("ws_one"),
                TaffyDestination.WorkspaceDetail("ws_two"),
            ),
            replaced.entries,
        )
    }

    @Test
    fun `going home clears everything above the start`() {
        val stack = BackStack.start()
            .push(TaffyDestination.SettingsHome)
            .push(TaffyDestination.Appearance)

        assertEquals(BackStack.start(), stack.home())
    }

    @Test
    fun `replacing all forgets even the bottom of the stack`() {
        val stack = BackStack.start(TaffyDestination.SettingsHome)
            .push(TaffyDestination.Appearance)
            .push(TaffyDestination.Notifications)

        val replaced = stack.replaceAll(TaffyDestination.BrowserMain)

        assertEquals(listOf(TaffyDestination.BrowserMain), replaced.entries)
        assertFalse(replaced.canGoBack)
    }

    @Test
    fun `popWhile leaves the first destination that is not in the group`() {
        val stack = BackStack.start()
            .push(TaffyDestination.SettingsHome)
            .push(TaffyDestination.Appearance)

        val left = stack.popWhile(TaffyDestinationGroups::isSettings)

        assertEquals(TaffyDestination.START, left.current)
        assertFalse(left.canGoBack)
    }

    @Test
    fun `popWhile does nothing when the top is not in the group`() {
        val stack = BackStack.start().push(TaffyDestination.Downloads)

        assertEquals(stack, stack.popWhile(TaffyDestinationGroups::isSettings))
    }

    @Test
    fun `popWhile leaves a whole you group in one step`() {
        val stack = BackStack.start()
            .push(TaffyDestination.You)
            .push(TaffyDestination.Memory)

        val left = stack.popWhile(TaffyDestinationGroups::isYou)

        assertEquals(TaffyDestination.START, left.current)
        assertFalse(left.canGoBack)
    }

    @Test
    fun `popWhile leaves a whole library group in one step`() {
        val stack = BackStack.start()
            .push(TaffyDestination.LibraryHome)
            .push(TaffyDestination.LibraryCollection("col_one"))

        val left = stack.popWhile(TaffyDestinationGroups::isLibrary)

        assertEquals(TaffyDestination.START, left.current)
        assertFalse(left.canGoBack)
    }

    @Test
    fun `what a pop leaves behind is what a host may clear`() {
        val before = BackStack.start().push(TaffyDestination.Appearance)
        val after = before.pop()

        assertEquals(listOf(TaffyDestination.Appearance), after.popped(before))
    }

    /**
     * The question a caller who may only *return* to a destination has to ask
     * first, and the reason it exists: `replaceCurrent` returns to a
     * destination the stack holds and opens one it does not, and the fork's
     * inbound-link path may only do the first. See `TaffyShellNavigation`.
     */
    @Test
    fun `a stack holds a destination anywhere on it, current included`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain)
            .push(TaffyDestination.SettingsHome)
            .push(TaffyDestination.Appearance)

        assertTrue(stack.holds(TaffyDestination.BrowserMain))
        assertTrue(stack.holds(TaffyDestination.SettingsHome))
        assertTrue(stack.holds(TaffyDestination.Appearance))
    }

    /**
     * First run, which is the case the whole predicate is for: the sequence's
     * stack does not reach the browsing surface, so nothing may return to it.
     */
    @Test
    fun `a stack does not hold a destination that was never opened`() {
        val sequence = BackStack.start(TaffyDestination.OnboardingWelcome)
            .push(TaffyDestination.LanguageRegion)

        assertFalse(sequence.holds(TaffyDestination.BrowserMain))
    }

    @Test
    fun `a destination left behind is no longer held`() {
        val stack = BackStack.start(TaffyDestination.BrowserMain)
            .push(TaffyDestination.AddressBar)
            .replaceCurrent(TaffyDestination.BrowserMain)

        assertFalse(stack.holds(TaffyDestination.AddressBar))
    }

    /**
     * Matched on the route, so a destination with arguments is held only when
     * its arguments are the same — the same rule `replaceCurrent` obeys, and
     * the two must not disagree about what "the stack already has this" means.
     */
    @Test
    fun `holding is decided by the route, arguments included`() {
        val stack = BackStack.start().push(TaffyDestination.WorkspaceDetail("ws_one"))

        assertTrue(stack.holds(TaffyDestination.WorkspaceDetail("ws_one")))
        assertFalse(stack.holds(TaffyDestination.WorkspaceDetail("ws_two")))
    }
}
