// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * The back stack as an immutable value.
 *
 * Navigation is a reducer like everything else in the UI layer: `push` and `pop`
 * are pure functions of the current stack, which is what lets the navigation
 * rules be unit tests instead of instrumentation tests.
 */
data class BackStack(
    /** Bottom first. Never empty: the start destination is always beneath. */
    val entries: List<TaffyDestination>,
) {
    /** What the user is looking at. */
    val current: TaffyDestination
        get() = entries.last()

    /** Whether there is anywhere to go back to. */
    val canGoBack: Boolean
        get() = entries.size > 1

    /**
     * Open a destination. Opening the one already on top is a no-op rather than
     * a second copy, so a double tap does not build a stack of duplicates.
     */
    fun push(destination: TaffyDestination): BackStack =
        if (destination.route == current.route) this else BackStack(entries + destination)

    /** Go back one step, or stay put when there is nowhere to go. */
    fun pop(): BackStack = if (canGoBack) BackStack(entries.dropLast(1)) else this

    /**
     * Show [destination] in place of the current entry, which is left behind
     * rather than kept as somewhere to go back to.
     *
     * ## Why a browsing surface is returned to rather than opened again
     *
     * When the stack already holds [destination] lower down, this returns to
     * that entry and drops everything above it instead of stacking a second
     * copy. That is the case the browser is always in: screen SCR-101 sits at
     * the bottom, and the address bar, the tab switcher and the new-tab chooser
     * all stand on it. Each of them used to *push* SCR-101 when its work was
     * done, so every committed address left an SCR-103/SCR-101 pair behind and
     * the stack grew by two per navigation. What that cost the person was the
     * system back button: it popped the pair rather than the page, so back on a
     * page they had followed a link to opened the address bar they had already
     * left, and four presses were needed to leave a browser they had used once.
     *
     * [push] cannot express this. It refuses only a duplicate of the entry on
     * *top*, which is the one arrangement a transient surface is never in.
     *
     * The bottom entry is never dropped, because the start destination is
     * always beneath. A stack of one has no current entry to leave, so
     * [destination] opens above it.
     */
    fun replaceCurrent(destination: TaffyDestination): BackStack {
        val existing = entries.indexOfLast { it.route == destination.route }
        return when {
            existing >= 0 -> BackStack(entries.take(existing + 1))
            canGoBack -> BackStack(entries.dropLast(1) + destination)
            else -> BackStack(entries + destination)
        }
    }

    /**
     * Whether [destination] is somewhere on the stack, current entry included.
     *
     * The question [replaceCurrent] cannot be asked without answering: that
     * function returns to a destination the stack holds and *opens* one it does
     * not, and a caller who may only do the first has to be able to tell the
     * two apart first. The fork's inbound-intent path is that caller — a link
     * from another application returns to the browsing surface when there is
     * one to return to, and does not put one in front of somebody who is
     * halfway through first run.
     *
     * Matched on [TaffyDestination.route] rather than on the object, for the
     * same reason every other function here does: a destination with arguments
     * is the same entry only when its arguments are the same, and `route` is
     * where those are written down.
     */
    fun holds(destination: TaffyDestination): Boolean =
        entries.any { it.route == destination.route }

    /** Everything the stack has left behind, which is what a host may clear. */
    fun popped(previous: BackStack): List<TaffyDestination> =
        previous.entries.filter { entry -> entries.none { it.route == entry.route } }

    /** Return to the bottom of the stack. */
    fun home(): BackStack = BackStack(listOf(entries.first()))

    /**
     * Throw the whole stack away and start over at [destination]. Finishing
     * the first-run sequence uses this: the sequence's screens are not
     * history the browser should walk back into.
     */
    fun replaceAll(destination: TaffyDestination): BackStack = BackStack(listOf(destination))

    /**
     * Pop while [shouldPop] holds and the stack can go back. One call leaves
     * a whole group (settings, a workspace) rather than one destination.
     */
    fun popWhile(shouldPop: (TaffyDestination) -> Boolean): BackStack {
        var next = this
        while (next.canGoBack && shouldPop(next.current)) {
            next = next.pop()
        }
        return next
    }

    companion object {
        /** A stack holding only the start destination. */
        fun start(destination: TaffyDestination = TaffyDestination.START): BackStack =
            BackStack(listOf(destination))
    }
}
