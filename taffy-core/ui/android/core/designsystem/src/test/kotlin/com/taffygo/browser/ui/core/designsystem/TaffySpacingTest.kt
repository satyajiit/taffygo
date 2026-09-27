// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.ui.unit.Dp
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Adaptive gutters from `handoff/DESIGN.md` section 5: the phone keeps the
 * catalog's sixteen-unit margin, the tablet steps up, and the touch target
 * does not change with width.
 */
class TaffySpacingTest {

    @Test
    fun `compact keeps the catalog phone gutter`() {
        assertEquals(16f, TaffySpacing.Compact.screenMargin.value)
        assertEquals(14f, TaffySpacing.Compact.cardPadding.value)
        assertEquals(Dp.Unspecified, TaffySpacing.Compact.chromeMaxWidth)
    }

    @Test
    fun `expanded uses the handoff tablet gutter`() {
        val expanded = TaffySpacing.Expanded

        assertEquals(24f, expanded.screenMargin.value)
        assertTrue(expanded.paneGap.value >= 16f)
        assertTrue(expanded.chromeMaxWidth.value > 0f)
    }

    @Test
    fun `the touch target is the same at every width`() {
        val compact = TaffySpacing.forWidth(TaffyWindowWidth.COMPACT)
        val medium = TaffySpacing.forWidth(TaffyWindowWidth.MEDIUM)
        val expanded = TaffySpacing.forWidth(TaffyWindowWidth.EXPANDED)

        assertEquals(compact.minimumTouchTarget, medium.minimumTouchTarget)
        assertEquals(compact.minimumTouchTarget, expanded.minimumTouchTarget)
        assertEquals(48f, compact.minimumTouchTarget.value)
        assertEquals(14f, compact.cardPadding.value)
        assertEquals(compact.cardPadding, medium.cardPadding)
        assertEquals(compact.cardPadding, expanded.cardPadding)
    }

    @Test
    fun `the gutter never shrinks as the window grows`() {
        val compact = TaffySpacing.forWidth(TaffyWindowWidth.COMPACT)
        val medium = TaffySpacing.forWidth(TaffyWindowWidth.MEDIUM)
        val expanded = TaffySpacing.forWidth(TaffyWindowWidth.EXPANDED)

        assertTrue(medium.screenMargin >= compact.screenMargin)
        assertTrue(expanded.screenMargin >= medium.screenMargin)
    }
}
