// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf

/**
 * Where the one hosted page surface is, as the screen carrying it reports it.
 *
 * The page is a `SurfaceView`, and a `SurfaceView`'s surface belongs to the
 * window rather than to the view. Leaving the window destroys it, and the next
 * frame after it comes back is black until the compositor has drawn into the
 * new one — which is the flash a person sees coming back from Settings, because
 * the navigation host disposes the outgoing composition and the page's
 * `AndroidView` goes with it.
 *
 * So the surface is composed once, above the navigation host's own content, and
 * never unmounted. A screen that carries the page does not compose it any more:
 * it reports where the page should be, through this slot, and the host places
 * it there. A destination that does not carry the page **parks** it — same size,
 * still attached, moved off the bottom of the window. Never hidden: hiding a
 * `SurfaceView` is a surface teardown, which is the defect.
 *
 * The numbers are px and are computed during composition by the reporting
 * screen, not measured from it. The host reads them inside a measure lambda, so
 * a report and the placement it causes land in the same frame.
 */
@Stable
class PageSurfaceSlot {

    /** Where the surface is right now. Parked until a screen says otherwise. */
    var placement: Placement by mutableStateOf(Placement.PARKED)
        private set

    /**
     * The [Placement.PARKED] ground has no colour of its own.
     *
     * A claim is held by identity rather than by a count, because two screens
     * that both carry the page are composed together for one frame while the
     * host swaps them. The screen arriving reports last and wins; the screen
     * leaving may only park what it still owns, so it cannot park the page out
     * from under its successor.
     */
    private var owner: Any? = null

    /** Puts the surface on screen, inset by [topPx]/[bottomPx] and slid down. */
    fun place(
        owner: Any,
        topPx: Int,
        bottomPx: Int,
        slidePx: Int,
        groundArgb: Int?,
    ) {
        this.owner = owner
        placement = Placement(
            onScreen = true,
            topPx = topPx,
            bottomPx = bottomPx,
            slidePx = slidePx,
            groundArgb = groundArgb,
        )
    }

    /** Parks the surface, if this owner is still the one holding the claim. */
    fun park(owner: Any) {
        if (this.owner !== owner) return
        this.owner = null
        placement = Placement.PARKED
    }

    /**
     * One placement of the page.
     *
     * [groundArgb] is the colour the page's own document reported, painted
     * beneath the surface by the host. It travels with the placement because
     * the screen that knows it is no longer the screen that draws it.
     */
    data class Placement(
        val onScreen: Boolean,
        val topPx: Int,
        val bottomPx: Int,
        val slidePx: Int,
        val groundArgb: Int?,
    ) {
        internal companion object {
            val PARKED = Placement(
                onScreen = false,
                topPx = 0,
                bottomPx = 0,
                slidePx = 0,
                groundArgb = null,
            )
        }
    }
}

/**
 * The hosted page slot, or null where nothing hosts one.
 *
 * Null is the default on purpose: every preview and every semantics test then
 * composes the page exactly where it always did, inside the screen, with no
 * host above it to report to.
 */
val LocalPageSurfaceSlot = staticCompositionLocalOf<PageSurfaceSlot?> { null }
