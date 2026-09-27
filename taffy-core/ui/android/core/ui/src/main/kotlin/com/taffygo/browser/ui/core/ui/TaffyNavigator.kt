// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * How a feature moves between screens.
 *
 * A feature holds this interface and the destination type, never another
 * feature's code. The application shell implements it over the back stack it
 * owns.
 */
interface TaffyNavigator {

    /** Open [destination], pushing it onto the back stack. */
    fun goTo(destination: TaffyDestination)

    /**
     * Show [destination] in place of the current screen, which is left behind
     * rather than kept as somewhere to go back to.
     *
     * This is what a surface that has finished its job uses — the address bar
     * once an address is committed, the tab switcher once a tab is chosen. A
     * destination the stack already holds is *returned to*, with everything
     * above it left behind, rather than stacked a second time. See
     * [BackStack.replaceCurrent] for what pushing instead cost the person.
     */
    fun replaceCurrent(destination: TaffyDestination)

    /** Go back one step. Returns false when there is nowhere to go. */
    fun goBack(): Boolean

    /**
     * Put the browser behind whatever the person was doing before, without
     * closing any tab.
     *
     * Back on a page with no history uses this, which is what a browser does
     * rather than walking through chrome — the tab switcher, a start page, a
     * "nothing open" card. Returns false when the host has no window to
     * background, which is every preview and every test that has not wired
     * one. The default is that false: a fake navigator is not a window.
     */
    fun leaveToBackground(): Boolean = false

    /** Return to the start destination, clearing everything above it. */
    fun goHome()

    /**
     * Replace the whole stack with [destination]. Leaving the first-run
     * sequence for the browser uses this, so back does not return to setup.
     */
    fun restart(destination: TaffyDestination)

    /**
     * Pop while [shouldPop] holds. The shell uses this to leave a settings or
     * workspace group in one step from the list pane.
     */
    fun popWhile(shouldPop: (TaffyDestination) -> Boolean)
}
