// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.runtime.Composable

/**
 * The four edges of the window, named once.
 *
 * Every surface in this application fills the screen and draws its own
 * background to the glass. What must not reach the glass is the part a person
 * reads or touches, and these are the four amounts by which it is pulled back.
 *
 * They all come from `safeDrawing`, which is the union of the system bars, the
 * display cutout and the keyboard — the three things that can cover what is
 * drawn. Naming them here rather than writing the expression at each call site
 * is not tidiness: a screen that reached for `statusBars` instead would be
 * correct on a phone with no camera in the way and wrong on every phone that
 * has one, and the difference would not show up until someone held the device.
 *
 * **Each of these is consumed where it is applied.** Applying [top] to a title
 * bar leaves the rest of the tree to apply [bottom] and [sides] without
 * counting the same pixels twice — that arithmetic is Compose's, not the
 * caller's, and it only works if every surface goes through a padding modifier
 * rather than reading a number and adding it by hand.
 */
object TaffyEdges {

    /** The status bar, and a cutout that reaches into it. */
    val top: WindowInsets
        @Composable
        get() = WindowInsets.safeDrawing.only(WindowInsetsSides.Top)

    /**
     * The navigation bar, and the keyboard when it is open.
     *
     * The keyboard is part of this on purpose. A field pushed above the
     * navigation bar and then covered by the keyboard is no better placed than
     * one that was never pushed at all, and the two amounts are never both
     * needed — the keyboard covers the navigation bar while it is up.
     */
    val bottom: WindowInsets
        @Composable
        get() = WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom)

    /**
     * The navigation bar, and never the keyboard.
     *
     * For a bar that is pinned to the bottom of the window and should stay
     * there — the browsing screen's action row. [bottom] lifts what it pads
     * clear of the keyboard, which is right for a field somebody is typing
     * into and wrong for a row of buttons: a keyboard is already the thing the
     * person is using, and a row of controls hopping up to sit on top of it is
     * a bar that moves for no reason and eats the space the keyboard needs.
     * The keyboard covers this instead, the way it covers one on every other
     * browser, and the row is where it was when the keyboard goes.
     *
     * Anything in that bar's stack that a person actually types into — find in
     * page, the form Taffy holds open — still has to clear the keyboard, and
     * does: see `BrowserPageChrome`, which lifts those and leaves this alone.
     */
    val bottomBar: WindowInsets
        @Composable
        get() = WindowInsets.systemBars.union(WindowInsets.displayCutout)
            .only(WindowInsetsSides.Bottom)

    /**
     * The keyboard alone, while it is open, and nothing when it is closed.
     *
     * For a surface that must rise above the keyboard as a whole — measuring
     * its own height against what the keyboard leaves — while still reaching
     * the bottom edge of the window once the keyboard is gone. A surface that
     * only needs its last control kept clear wants [bottom] instead.
     */
    val keyboard: WindowInsets
        @Composable
        get() = WindowInsets.ime

    /**
     * The left and right edges: a cutout once the device is turned on its
     * side, the navigation bar in the same position, and the curve of a
     * display whose glass bends away.
     *
     * These are zero on most phones held upright, which is exactly why they
     * are easy to forget and worth naming.
     */
    val sides: WindowInsets
        @Composable
        get() = WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal)

    /** The sides and the bottom, for a surface whose own title bar took the top. */
    val sidesAndBottom: WindowInsets
        @Composable
        get() = WindowInsets.safeDrawing.only(
            WindowInsetsSides.Horizontal + WindowInsetsSides.Bottom,
        )

    /** Everything at once, for a surface with no bar of its own at either end. */
    val all: WindowInsets
        @Composable
        get() = WindowInsets.safeDrawing

    /**
     * The top and bottom edges for the web page's own surface, and nothing
     * else in this object is right for it.
     *
     * **This is the one member not derived from `safeDrawing`, and the reason
     * is the keyboard.** Not because the page ignores the keyboard — it does
     * not, and a page that did left a person typing behind one — but because
     * the keyboard is a different kind of amount here and is composed in
     * separately by `BrowserPageArea`, along with the action row's height and
     * the top bar's offset. This member is the window's own share and nothing
     * else, so the arithmetic that decides a viewport lives in one place
     * rather than half-hidden inside a token. `systemBars` and the display
     * cutout do not come and go, so this much is settled once for the life of
     * a window shape.
     *
     * It is a union rather than `safeDrawing.exclude(ime)`, and the difference
     * matters wherever the two are combined: `exclude` subtracts side by side,
     * and the keyboard's inset is the larger of the two at the bottom while it
     * is up, so subtracting it would take the navigation bar away with it at
     * exactly the moment it was still there. `BrowserPageArea` takes the larger
     * of the two for the same reason.
     *
     * This is the window's own share and not the whole viewport. TaffyGo's two
     * bars come out of the page as well — decision 0119 section 2 — and their
     * heights are added to this by the surface that draws the page, because
     * this object knows about windows and nothing about chrome. The keyboard
     * stays out of both halves for the reason above.
     */
    val page: WindowInsets
        @Composable
        get() = WindowInsets.systemBars.union(WindowInsets.displayCutout)
            .only(WindowInsetsSides.Vertical)
}
