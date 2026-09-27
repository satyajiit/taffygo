// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * How the selected page should colour the window and whether the browser's
 * chrome is showing.
 *
 * This is not [NavigationState]. Navigation is what the tab is showing;
 * this is how the chrome around it should behave while it is showing it.
 * Scroll and page-background callbacks arrive many times a second, and a
 * second flow keeps those updates from rebuilding the address pill on every
 * finger movement.
 *
 * [backgroundArgb] is the page's own ground, or null when there is no page
 * — a blank tab, a failure, a browser that has not started. The window
 * paints the theme surface in that case rather than inventing a colour.
 */
data class PageAppearance(
    /**
     * The page background as an opaque ARGB colour, or null when the chrome
     * should use the theme surface instead.
     */
    val backgroundArgb: Int? = null,
    /**
     * Whether the top bar — the address pill and the overflow — is showing.
     *
     * The top bar alone. The action row at the bottom of screen SCR-101 does
     * not answer to this and does not answer to anything: it is always on
     * screen, so the page is measured above it rather than drawn under it.
     * That is what makes a page's own bottom-anchored content — a cookie
     * banner and its buttons, a chat widget, a checkout bar — reachable at
     * all, and it costs no relayout because a bar that never moves cannot
     * cause one.
     *
     * Hidden while the person is scrolling down a page, the way a browser's
     * address bar hides; a scroll up or a tap brings it back. Always true on
     * a tab that has been nowhere, because there is no page to reclaim the
     * space from.
     */
    val topBarVisible: Boolean = true,
)
