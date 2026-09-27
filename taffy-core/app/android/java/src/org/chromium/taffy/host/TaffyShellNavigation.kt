// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator

/**
 * How an intent from another application reaches the interface's own back stack.
 *
 * WHY THIS EXISTS. `TaffyBrowserActivity` opens the tab an inbound link asks
 * for, and until this class it stopped there. Measured on a device: with screen
 * SCR-407 open, an inbound `VIEW` intent created the tab and loaded it, and
 * left Appearance on screen. The person's link had happened, in a place they
 * could not see, and the only way to find it was to guess that going back twice
 * would show it. A browser that opens a link where nobody is looking has not
 * opened it.
 *
 * WHY IT IS A SEAM AND NOT A CALL. The back stack belongs to the composition —
 * [ShellViewModel][com.taffygo.browser.ui.app.ShellViewModel] owns it and hands
 * out a [TaffyNavigator] over it — and the composition is built inside
 * `triggerLayoutInflation()`, long before the intent that needs it arrives. So
 * the activity cannot hold the navigator, and the composition cannot know about
 * intents. This is the object both sides can hold: the activity builds one and
 * passes it to [TaffyShellViews.of], the composition [attach]es its navigator
 * to it, and the activity asks it for one thing. It is the same shape as
 * [TaffyWindowBars] with the direction reversed — that one carries a decision
 * out of the composition, this one carries a request in.
 *
 * WHY THE BROWSING SURFACE IS *RETURNED TO* AND NOT OPENED. [showBrowsingSurface]
 * moves the stack only when screen SCR-101 is already on it, and answers false
 * when it is not. The case that rule is written for is first run: the sequence
 * starts a stack of its own, SCR-101 is not under it, and an application sending
 * a link while a person is halfway through setting up the browser may not skip
 * them past it. The tab is still opened — it is waiting when the sequence ends
 * on the browsing surface, which is where `restart` lands. What may not happen
 * is another application deciding which screen TaffyGo shows.
 *
 * Everything here is a pure decision over a [BackStack] plus one call, so
 * `TaffyShellNavigationTest` drives all of it on a laptop.
 */
class TaffyShellNavigation {

    /**
     * The composition's navigator and its stack, or null when nothing is
     * composed.
     *
     * `@Volatile` because the two sides run on the same thread and this class
     * may not depend on that staying true: the activity's callers are Android
     * lifecycle callbacks and the composition attaches from an effect. A torn
     * read here would be an inbound link landing nowhere, which is the defect
     * this file exists to remove.
     */
    @Volatile
    private var attached: Attachment? = null

    /** What [showBrowsingSurface] needs, held together so neither half can be stale. */
    private class Attachment(val navigator: TaffyNavigator, val backStack: () -> BackStack)

    /**
     * Points this at the composition's own navigation.
     *
     * Replaces whatever was attached, so a composition that is rebuilt — a
     * theme change, a language change, a rebuilt activity — swaps rather than
     * doubles.
     *
     * @param navigator the interface's navigator, over the stack [backStack] reads.
     * @param backStack reads the current stack. A function rather than a value,
     *   because the stack is immutable and replaced on every navigation: a
     *   value captured at attach time would answer for the screen the person
     *   was on when the browser started.
     */
    fun attach(navigator: TaffyNavigator, backStack: () -> BackStack) {
        attached = Attachment(navigator, backStack)
    }

    /** Takes it away again, so nothing holds a destroyed activity's navigator. */
    fun detach() {
        attached = null
    }

    /**
     * Brings the browsing surface to the front, if the person is somewhere it
     * can be brought to the front from.
     *
     * @return whether the stack moved. False means one of two honest things:
     *   nothing is composed yet, or screen SCR-101 is not on the stack at all —
     *   see the class comment for why the second is a refusal rather than a
     *   failure. A caller that has just opened a tab does not need to do
     *   anything differently either way; the answer exists so a test can tell
     *   the two apart.
     */
    /**
     * Puts the page an errand was just opened on in front of the person.
     *
     * **It has no equivalent of [showBrowsingSurface]'s guard, and that is the
     * decision rather than an omission.** That guard refuses when screen SCR-101
     * is not on the stack, because another application may not skip a person
     * past first run. This is the opposite case in every respect: nothing
     * external asked for it, the person did — they pressed Sign in — and one of
     * the places they can press it is the first-run sequence itself, where
     * SCR-101 is not on the stack at all. Copying the guard here would refuse
     * every account sign-in during first run, which is the flow this seam was
     * added for.
     *
     * Pushed rather than replacing, because an errand is a page a person comes
     * back from: back returns to the screen that sent them, with whatever they
     * had typed into it still there.
     *
     * @return whether the stack moved. False means nothing is composed yet,
     *   which is a page opened where nobody can see it — the caller answers for
     *   that by not claiming it opened anything.
     */
    fun showErrandPage(errandId: String): Boolean {
        val attachment = attached ?: return false
        if (errandId.isEmpty()) return false
        attachment.navigator.goTo(TaffyDestination.ErrandPage(errandId))
        return true
    }

    fun showBrowsingSurface(): Boolean {
        val attachment = attached ?: return false
        if (!attachment.backStack().holds(TaffyDestination.BrowserMain)) return false
        // `replaceCurrent` is what "return to" is spelled as: the stack holds
        // SCR-101, so it drops everything above it rather than stacking a
        // second copy. See `BackStack.replaceCurrent` for the defect that rule
        // was written for.
        attachment.navigator.replaceCurrent(TaffyDestination.BrowserMain)
        return true
    }
}
