// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The fold rule, as the executable form of decision 0103 section 4: two
 * columns on a phone, one at large text or under 340 dp, three and four on
 * the wider buckets — and the hero card stacks by the same numbers.
 */
class TaffyBentoColumnsTest {

    @Test
    fun `a phone draws two columns`() {
        assertEquals(2, TaffyBentoColumns.of(availableWidthDp = 360, fontScale = 1.0f))
        assertEquals(2, TaffyBentoColumns.of(availableWidthDp = 412, fontScale = 1.15f))
    }

    @Test
    fun `folds to one column at large text`() {
        assertEquals(1, TaffyBentoColumns.of(availableWidthDp = 360, fontScale = 1.3f))
        assertEquals(1, TaffyBentoColumns.of(availableWidthDp = 360, fontScale = 2.0f))
        assertEquals(2, TaffyBentoColumns.of(availableWidthDp = 360, fontScale = 1.29f))
    }

    @Test
    fun `folds to one column under 340 dp`() {
        assertEquals(1, TaffyBentoColumns.of(availableWidthDp = 339, fontScale = 1.0f))
        assertEquals(2, TaffyBentoColumns.of(availableWidthDp = 340, fontScale = 1.0f))
        assertTrue(TaffyBentoColumns.folds(availableWidthDp = 0, fontScale = 1.0f))
        assertFalse(TaffyBentoColumns.folds(availableWidthDp = 340, fontScale = 1.0f))
    }

    @Test
    fun `a medium width draws three and an expanded width four`() {
        assertEquals(2, TaffyBentoColumns.of(availableWidthDp = 599, fontScale = 1.0f))
        assertEquals(3, TaffyBentoColumns.of(availableWidthDp = 600, fontScale = 1.0f))
        assertEquals(3, TaffyBentoColumns.of(availableWidthDp = 839, fontScale = 1.0f))
        assertEquals(4, TaffyBentoColumns.of(availableWidthDp = 840, fontScale = 1.0f))
        assertEquals(4, TaffyBentoColumns.of(availableWidthDp = 1024, fontScale = 1.0f))
    }

    @Test
    fun `a tablet list pane draws the columns its own width earns`() {
        // The primary pane of a 1024 dp list-detail split is about 400 dp;
        // a window-bucketed rule would draw four columns in it.
        assertEquals(2, TaffyBentoColumns.of(availableWidthDp = 400, fontScale = 1.0f))
    }

    @Test
    fun `large text folds a tablet too`() {
        assertEquals(1, TaffyBentoColumns.of(availableWidthDp = 1024, fontScale = 1.3f))
    }

    @Test
    fun `the hero and the grid share one fold`() {
        assertEquals(TaffyBentoColumns.FOLD_WIDTH_DP, TaffyHeroStackedWidthDp)
        assertEquals(TaffyBentoColumns.FOLD_FONT_SCALE, TaffyHeroStackedFontScale)
    }
}
