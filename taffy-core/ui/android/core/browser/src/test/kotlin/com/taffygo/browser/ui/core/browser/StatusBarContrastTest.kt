// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Icon polarity from page ground, including the hysteresis a live colour needs.
 */
class StatusBarContrastTest {

    @Test
    fun `white is a light ground, so the icons are dark`() {
        assertTrue(StatusBarContrast.iconsAreDark(OPAQUE_WHITE))
    }

    @Test
    fun `black is a dark ground, so the icons are light`() {
        assertFalse(StatusBarContrast.iconsAreDark(OPAQUE_BLACK))
    }

    @Test
    fun `a saturated yellow is still a light ground`() {
        // Channel-average would call this mid; luminance does not. This is
        // the reason the measurement is WCAG's rather than a mean of RGB.
        assertTrue(StatusBarContrast.iconsAreDark(OPAQUE_YELLOW))
        assertTrue(StatusBarContrast.relativeLuminance(OPAQUE_YELLOW) > 0.8)
    }

    @Test
    fun `the first colour is always taken`() {
        assertEquals(OPAQUE_WHITE, StatusBarContrast.stabilize(OPAQUE_WHITE, last = null))
    }

    @Test
    fun `a colour a few levels away is treated as the same ground`() {
        // 0xFDFDFD is three levels off white on each channel — compression,
        // not a new page.
        val almostWhite = 0xFFFDFDFD.toInt()
        assertEquals(OPAQUE_WHITE, StatusBarContrast.stabilize(almostWhite, last = OPAQUE_WHITE))
    }

    @Test
    fun `a real theme change is published`() {
        assertEquals(OPAQUE_BLACK, StatusBarContrast.stabilize(OPAQUE_BLACK, last = OPAQUE_WHITE))
    }

    @Test
    fun `polarity does not flip inside the dead band`() {
        // Channel 0xB6 is about 0.48 luminance — just below the 0.5 midpoint,
        // so it would flip icons relative to white, and inside the dead band
        // so it must not.
        val justBelowMid = 0xFFB6B6B6.toInt()
        assertTrue(StatusBarContrast.relativeLuminance(justBelowMid) > 0.45)
        assertTrue(StatusBarContrast.relativeLuminance(justBelowMid) < 0.5)
        assertEquals(
            OPAQUE_WHITE,
            StatusBarContrast.stabilize(justBelowMid, last = OPAQUE_WHITE),
        )
    }

    private companion object {
        const val OPAQUE_WHITE = 0xFFFFFFFF.toInt()
        const val OPAQUE_BLACK = 0xFF000000.toInt()
        const val OPAQUE_YELLOW = 0xFFFFFF00.toInt()
    }
}
