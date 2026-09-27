// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem.internal

import kotlin.math.pow

/**
 * The contrast ratio of two opaque colours, by the accessibility guidelines'
 * relative-luminance formula.
 *
 * Written here rather than taken from a library because it is nine lines and
 * because the parity row it serves — status is legible in both themes — has to
 * be checkable without a device.
 */
internal object ContrastRatio {

    /** Normal text needs this much contrast against its background. */
    const val TEXT_MINIMUM = 4.5

    /** A component boundary or a graphical object needs this much. */
    const val GRAPHIC_MINIMUM = 3.0

    /** The ratio between two opaque colour words, from 1 to 21. */
    fun between(first: Long, second: Long): Double {
        val brighter = maxOf(relativeLuminance(first), relativeLuminance(second))
        val darker = minOf(relativeLuminance(first), relativeLuminance(second))
        return (brighter + 0.05) / (darker + 0.05)
    }

    /**
     * The relative luminance of one opaque colour word, from 0 to 1.
     *
     * Visible to the module rather than private because a ratio cannot say
     * which of two colours is the darker one, and the scrim assertion needs
     * exactly that: a dim has to *lower* what is behind it, not merely differ
     * from it. This object is already `internal`, so widening one member of it
     * exposes nothing outside `:core:designsystem`.
     */
    fun relativeLuminance(color: Long): Double {
        val red = channel(color shr 16)
        val green = channel(color shr 8)
        val blue = channel(color)
        return 0.2126 * red + 0.7152 * green + 0.0722 * blue
    }

    private fun channel(shifted: Long): Double {
        val value = (shifted and 0xFF).toDouble() / 255.0
        return if (value <= 0.03928) value / 12.92 else ((value + 0.055) / 1.055).pow(2.4)
    }
}
