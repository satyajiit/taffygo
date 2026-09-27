// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The Material 3 width buckets, held to their numbers so a screen never
 * invents a private breakpoint.
 */
class TaffyWindowWidthTest {

    @Test
    fun `a phone width is compact and stays one pane`() {
        val width = TaffyWindowWidth.fromWidth(PhoneWindowWidth)

        assertEquals(TaffyWindowWidth.COMPACT, width)
        assertFalse(width.showsTwoPane)
        assertFalse(width.usesTabletType)
    }

    @Test
    fun `the compact bucket stops just below the medium boundary`() {
        assertEquals(
            TaffyWindowWidth.COMPACT,
            TaffyWindowWidth.fromWidthDp(TaffyWindowWidth.MEDIUM_MIN_DP - 1),
        )
    }

    @Test
    fun `a foldable inner width is medium and shows two panes`() {
        val width = TaffyWindowWidth.fromWidthDp(TaffyWindowWidth.MEDIUM_MIN_DP)

        assertEquals(TaffyWindowWidth.MEDIUM, width)
        assertTrue(width.showsTwoPane)
        assertFalse(width.usesTabletType)
    }

    @Test
    fun `the handoff tablet frame is expanded and takes the tablet type`() {
        val width = TaffyWindowWidth.fromWidth(TabletWindowWidth)

        assertEquals(TaffyWindowWidth.EXPANDED, width)
        assertTrue(width.showsTwoPane)
        assertTrue(width.usesTabletType)
    }

    @Test
    fun `the expanded bucket begins at the platform boundary`() {
        assertEquals(
            TaffyWindowWidth.EXPANDED,
            TaffyWindowWidth.fromWidthDp(TaffyWindowWidth.EXPANDED_MIN_DP),
        )
        assertEquals(
            TaffyWindowWidth.MEDIUM,
            TaffyWindowWidth.fromWidthDp(TaffyWindowWidth.EXPANDED_MIN_DP - 1),
        )
    }
}
