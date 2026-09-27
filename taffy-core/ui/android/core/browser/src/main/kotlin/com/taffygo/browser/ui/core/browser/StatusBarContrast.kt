// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import kotlin.math.pow
import kotlin.math.abs

/**
 * Whether the status-bar icons should be dark, given the ground they sit on.
 *
 * The window's bars are transparent, so the only decision that reaches a
 * person is icon polarity. Android's helper picks it from luminance of the
 * colour it is handed; this is the same measurement, with two extra rules
 * a page-driven colour needs and a theme surface does not:
 *
 * 1. **WCAG relative luminance**, not a naive average. A saturated yellow
 *    and a mid grey can share a channel mean and disagree about whether
 *    black icons would read.
 * 2. **Hysteresis around the midpoint.** A page that paints 0.49 then 0.51
 *    then 0.49 as it loads would flip the clock and the battery on every
 *    frame. [stabilize] keeps the last colour through that band, and also
 *    drops updates that are only a few levels apart — compression noise,
 *    not a new ground.
 *
 * The 0.5 threshold is the same floor Chromium's `ColorUtils` uses for
 * "is this light". The dead band around it is this file's, because a
 * theme colour is stable and a live page background is not.
 */
object StatusBarContrast {

    /**
     * Whether dark (on-light) status-bar icons would contrast with [argb].
     *
     * True on a light ground. The window helper inverts this into the
     * `APPEARANCE_LIGHT_STATUS_BARS` bit: dark icons on a light page.
     */
    fun iconsAreDark(argb: Int): Boolean = relativeLuminance(argb) > LIGHT_GROUND

    /**
     * [candidate], or [last] when publishing it would flicker.
     *
     * A first colour has no last and is always taken. After that, a colour
     * within [CHANNEL_NOISE] of the last one is treated as the same ground,
     * and a polarity flip inside [DEAD_BAND] of the midpoint is refused.
     */
    fun stabilize(candidate: Int, last: Int?): Int {
        if (last == null) return candidate
        if (channelDistance(candidate, last) < CHANNEL_NOISE) return last
        val newDark = iconsAreDark(candidate)
        val lastDark = iconsAreDark(last)
        if (newDark != lastDark) {
            val luminance = relativeLuminance(candidate)
            if (luminance > LIGHT_GROUND - DEAD_BAND && luminance < LIGHT_GROUND + DEAD_BAND) {
                return last
            }
        }
        return candidate
    }

    /** WCAG 2 relative luminance of an opaque ARGB colour, 0 (black) to 1 (white). */
    fun relativeLuminance(argb: Int): Double {
        val red = linear((argb shr 16) and 0xFF)
        val green = linear((argb shr 8) and 0xFF)
        val blue = linear(argb and 0xFF)
        return 0.2126 * red + 0.7152 * green + 0.0722 * blue
    }

    private fun linear(channel: Int): Double {
        val srgb = channel / 255.0
        return if (srgb <= 0.04045) srgb / 12.92 else ((srgb + 0.055) / 1.055).pow(2.4)
    }

    private fun channelDistance(left: Int, right: Int): Int {
        val red = abs(((left shr 16) and 0xFF) - ((right shr 16) and 0xFF))
        val green = abs(((left shr 8) and 0xFF) - ((right shr 8) and 0xFF))
        val blue = abs((left and 0xFF) - (right and 0xFF))
        return red + green + blue
    }

    /** Midpoint: above this, the ground is light and icons should be dark. */
    private const val LIGHT_GROUND = 0.5

    /** Polarity does not flip inside this band either side of [LIGHT_GROUND]. */
    private const val DEAD_BAND = 0.05

    /**
     * Sum of per-channel 8-bit distances treated as the same colour.
     *
     * Twelve is a few levels on each channel; a real theme change is tens or
     * hundreds.
     */
    private const val CHANNEL_NOISE = 12
}
