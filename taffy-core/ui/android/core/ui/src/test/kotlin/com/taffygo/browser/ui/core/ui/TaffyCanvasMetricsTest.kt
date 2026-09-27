// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Destination-canvas numbers later tracks share. A You chapter and a stats
 * hero must not invent a second pair of sizes.
 */
class TaffyCanvasMetricsTest {

    @Test
    fun `chapter tiles are 106 by 76`() {
        assertEquals(106f, TaffyCategoryChapterMinHeight.value)
        assertEquals(76f, TaffyCategoryChapterHostSize.value)
        assertEquals(52f, TaffyCategoryHostSize.value)
    }

    @Test
    fun `hero art is a two-to-three cutout`() {
        assertEquals(214f, TaffyHeroArtWide.value)
        assertEquals(192f, TaffyHeroArtCompact.value)
        assertEquals(158f, TaffyHeroArtStacked.value)
        assertEquals(1.5f, TaffyHeroArtHeightFactor)
        assertEquals(237f, taffyHeroArtHeight(TaffyHeroArtStacked).value)
        assertEquals(321f, taffyHeroArtHeight(TaffyHeroArtWide).value)
        assertEquals(340, TaffyHeroStackedWidthDp)
        assertEquals(390, TaffyHeroCompactWidthDp)
        assertEquals(1.3f, TaffyHeroStackedFontScale)
        // The hero stacks by the grid's own fold, so the two cannot drift.
        assertEquals(TaffyBentoColumns.FOLD_WIDTH_DP, TaffyHeroStackedWidthDp)
        assertEquals(TaffyBentoColumns.FOLD_FONT_SCALE, TaffyHeroStackedFontScale)
    }

    @Test
    fun `soft pulse icon hosts are 44`() {
        assertEquals(44f, TaffyGlyphFrameHostSize.value)
    }

    @Test
    fun `identity busts are 44 52 64 72`() {
        assertEquals(44f, TaffyCharacterBustSmall.value)
        assertEquals(52f, TaffyCharacterBust.value)
        assertEquals(64f, TaffyCharacterBustLarge.value)
        assertEquals(72f, TaffyCharacterBustHero.value)
    }

    @Test
    fun `the canvas recipe is one named object`() {
        assertEquals("TaffyDestinationCanvas", TaffyDestinationCanvas::class.simpleName)
    }
}
