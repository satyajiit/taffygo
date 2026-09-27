// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * Which colour the status bar should stand on, given what the page reported.
 *
 * The page background is preferred: that is the ground that actually sits
 * under the status bar once the window is edge-to-edge. The document's
 * `theme-color` is the fallback when the background is missing or
 * transparent. A tab that has been nowhere has no page, so it has no colour
 * of its own — the theme surface answers instead, which is what `null`
 * means here.
 *
 * Alpha is the whole of the validity check. A transparent colour is not a
 * colour the icons can contrast against, and inventing an opaque version
 * of it would be this file deciding what the page looked like.
 */
object PageColor {

    /**
     * The colour to paint under the status bar, or `null` to use the theme
     * surface.
     *
     * [backgroundArgb] and [themeArgb] are packed ARGB ints as Android and
     * Chromium both use. Only fully opaque values are accepted.
     */
    fun of(
        hasBeenNowhere: Boolean,
        backgroundArgb: Int,
        themeArgb: Int,
        themingAllowed: Boolean,
    ): Int? {
        if (hasBeenNowhere) return null
        if (isOpaque(backgroundArgb)) return backgroundArgb
        if (themingAllowed && isOpaque(themeArgb)) return themeArgb
        return null
    }

    private fun isOpaque(argb: Int): Boolean = (argb ushr 24) == 0xFF
}
