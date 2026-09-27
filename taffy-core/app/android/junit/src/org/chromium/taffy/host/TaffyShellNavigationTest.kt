// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * What an inbound link does to the screen the person is looking at.
 *
 * WHAT THIS SUITE IS FOR. The defect it was written against was a link that
 * worked and could not be seen. With screen SCR-407 open on a device, an
 * inbound `VIEW` intent created the tab and loaded the page and left Appearance
 * on top of it — the tab count went up, nothing else moved, and the person had
 * to guess their way back twice to find what they had tapped. The two rules
 * below are what that costs to fix and what it may not cost: the browsing
 * surface comes forward from anywhere it is reachable from, and it is never
 * *opened* somewhere it was not.
 *
 * WHAT IT CANNOT PROVE. That the tab exists, or that the page loaded. Those are
 * `TaffyBrowserActivity`'s and the tab model's, and the evidence for them is a
 * run on a device. This is the one decision either side of that can get wrong
 * silently.
 */
@RunWith(BaseRobolectricTestRunner::class)
class TaffyShellNavigationTest {

    /**
     * The defect, as one assertion: two screens deep in settings, an inbound
     * link puts the browsing surface back in front and leaves nothing above it.
     */
    @Test
    fun `an inbound link returns to the browsing surface from inside settings`() {
        val navigator = RecordingNavigator(
            BackStack.start(TaffyDestination.BrowserMain)
                .push(TaffyDestination.SettingsHome)
                .push(TaffyDestination.Appearance),
        )
        val navigation = TaffyShellNavigation()
        navigation.attach(navigator) { navigator.stack }

        assertTrue(navigation.showBrowsingSurface())
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.stack.entries)
    }

    /**
     * Already there is not a reason to do anything, and not a reason to answer
     * no either: the surface the caller asked for is the one on screen.
     */
    @Test
    fun `an inbound link on the browsing surface leaves the stack alone`() {
        val navigator = RecordingNavigator(BackStack.start(TaffyDestination.BrowserMain))
        val navigation = TaffyShellNavigation()
        navigation.attach(navigator) { navigator.stack }

        assertTrue(navigation.showBrowsingSurface())
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.stack.entries)
    }

    /**
     * The rule that keeps this from becoming a second defect.
     *
     * During first run the stack is the sequence's own and SCR-101 is not under
     * it. An application sending a link may not push a person past a browser
     * they are still setting up — so the stack does not move, and the tab
     * `TaffyBrowserActivity` opened is waiting on the browsing surface the
     * sequence itself lands on.
     */
    @Test
    fun `an inbound link during first run does not open the browsing surface`() {
        val sequence = BackStack.start(TaffyDestination.OnboardingWelcome)
            .push(TaffyDestination.LanguageRegion)
        val navigator = RecordingNavigator(sequence)
        val navigation = TaffyShellNavigation()
        navigation.attach(navigator) { navigator.stack }

        assertFalse(navigation.showBrowsingSurface())
        assertEquals(sequence.entries, navigator.stack.entries)
        assertEquals(0, navigator.calls)
    }

    /**
     * Before the first composition and after the last there is nothing to
     * navigate, and saying so is the honest answer rather than a crash on a
     * link that arrived a moment too early.
     */
    @Test
    fun `an inbound link with nothing composed answers no`() {
        assertFalse(TaffyShellNavigation().showBrowsingSurface())
    }

    @Test
    fun `an inbound link after the composition is gone answers no`() {
        val navigator = RecordingNavigator(BackStack.start(TaffyDestination.Appearance))
        val navigation = TaffyShellNavigation()
        navigation.attach(navigator) { navigator.stack }
        navigation.detach()

        assertFalse(navigation.showBrowsingSurface())
        assertEquals(0, navigator.calls)
    }

    /**
     * A composition that is rebuilt — a language change, a rebuilt activity —
     * attaches again, and the second attachment is the one that answers.
     */
    @Test
    fun `attaching twice navigates the second stack and not the first`() {
        val gone = RecordingNavigator(BackStack.start(TaffyDestination.BrowserMain))
        val live = RecordingNavigator(
            BackStack.start(TaffyDestination.BrowserMain).push(TaffyDestination.Appearance),
        )
        val navigation = TaffyShellNavigation()
        navigation.attach(gone) { gone.stack }
        navigation.attach(live) { live.stack }

        assertTrue(navigation.showBrowsingSurface())
        assertEquals(0, gone.calls)
        assertEquals(listOf(TaffyDestination.BrowserMain), live.stack.entries)
    }

    /**
     * The stack is read now and not at attach time.
     *
     * The composition attaches once and the person navigates for hours
     * afterwards; a captured value would answer for the screen they were on
     * when the browser started, which is the same class of defect as the one
     * this file exists for.
     */
    @Test
    fun `the stack is read when the link arrives and not when the composition attached`() {
        val navigator = RecordingNavigator(BackStack.start(TaffyDestination.BrowserMain))
        val navigation = TaffyShellNavigation()
        navigation.attach(navigator) { navigator.stack }

        navigator.goTo(TaffyDestination.SettingsHome)
        navigator.goTo(TaffyDestination.Appearance)

        assertTrue(navigation.showBrowsingSurface())
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.stack.entries)
    }

    /**
     * The UI host's own navigator over a real [BackStack], so the rules asserted
     * here are the arithmetic the product runs rather than a restatement of it.
     * `ShellNavigator` itself is `internal` to the shell module and cannot be
     * named from this package, so this is the smallest faithful stand-in: every
     * operation delegates to the same [BackStack] functions it calls.
     */
    /**
     * The rule [TaffyShellNavigation.showErrandPage] exists to break.
     *
     * `showBrowsingSurface` refuses when screen SCR-101 is not on the stack,
     * because another application may not skip a person past first run. An
     * errand is the opposite case in every respect — the person asked for it,
     * by pressing Sign in — and one of the places they can press it is the
     * first-run sequence itself, where SCR-101 is not on the stack at all.
     * Copying that guard here would refuse every account sign-in during first
     * run, which is the flow the seam was added for.
     */
    @Test
    fun `an errand page opens from first run, where the browsing surface is not on the stack`() {
        val navigator = RecordingNavigator(BackStack.start(TaffyDestination.OnboardingWelcome))
        val navigation = TaffyShellNavigation()
        navigation.attach(navigator) { navigator.stack }

        assertTrue(navigation.showErrandPage("errand-1"))
        assertEquals(TaffyDestination.ErrandPage("errand-1"), navigator.stack.current)
    }

    /** Pushed, so back returns to the screen that sent the person on the errand. */
    @Test
    fun `an errand page is pushed over the screen that opened it`() {
        val navigator = RecordingNavigator(
            BackStack.start(TaffyDestination.BrowserMain)
                .push(TaffyDestination.SettingsHome)
                .push(TaffyDestination.ProviderSignIn("anthropic")),
        )
        val navigation = TaffyShellNavigation()
        navigation.attach(navigator) { navigator.stack }

        assertTrue(navigation.showErrandPage("errand-7"))

        assertEquals(TaffyDestination.ErrandPage("errand-7"), navigator.stack.current)
        navigator.goBack()
        assertEquals(TaffyDestination.ProviderSignIn("anthropic"), navigator.stack.current)
    }

    /**
     * Nothing composed is a page opened where nobody can see it, and the caller
     * answers for that by taking the page down rather than claiming it opened.
     */
    @Test
    fun `an errand page refuses when nothing is composed`() {
        assertFalse(TaffyShellNavigation().showErrandPage("errand-1"))
    }

    /** An errand with no identity names no page, so there is nothing to show. */
    @Test
    fun `an errand page refuses an empty identity`() {
        val navigator = RecordingNavigator(BackStack.start(TaffyDestination.BrowserMain))
        val navigation = TaffyShellNavigation()
        navigation.attach(navigator) { navigator.stack }

        assertFalse(navigation.showErrandPage(""))
        assertEquals(0, navigator.calls)
    }

    private class RecordingNavigator(initial: BackStack) : TaffyNavigator {

        var stack: BackStack = initial
            private set

        /** How many times something asked this navigator to move. */
        var calls: Int = 0
            private set

        override fun goTo(destination: TaffyDestination) {
            calls++
            stack = stack.push(destination)
        }

        override fun replaceCurrent(destination: TaffyDestination) {
            calls++
            stack = stack.replaceCurrent(destination)
        }

        override fun goBack(): Boolean {
            calls++
            val next = stack.pop()
            val moved = next != stack
            stack = next
            return moved
        }

        override fun goHome() {
            calls++
            stack = stack.home()
        }

        override fun restart(destination: TaffyDestination) {
            calls++
            stack = stack.replaceAll(destination)
        }

        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) {
            calls++
            stack = stack.popWhile(shouldPop)
        }
    }
}
