// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.unit.dp
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * A shape and a hand-drawn outline of that shape may not disagree.
 *
 * They did. `TabSwitcherScreen` restated the row radius as 16 dp beside a
 * comment saying it restated [TaffyShapes.row], which is 10, and the tab
 * switcher's dashed workspace offer was outlined six density-independent
 * pixels outside the shape clipping it. Nothing could see that, because the
 * two numbers lived in different files and only one of them was a token.
 * These assertions are the thing that now sees it.
 */
class TaffyRadiiTest {

    @Test
    fun `every shape is built from the radius of the same name`() {
        val shapes = TaffyShapes.Default

        assertEquals(RoundedCornerShape(TaffyRadii.chip), shapes.chip)
        assertEquals(RoundedCornerShape(TaffyRadii.row), shapes.row)
        assertEquals(RoundedCornerShape(TaffyRadii.tile), shapes.tile)
        assertEquals(RoundedCornerShape(TaffyRadii.card), shapes.card)
        assertEquals(
            RoundedCornerShape(topStart = TaffyRadii.sheet, topEnd = TaffyRadii.sheet),
            shapes.sheet,
        )
        assertEquals(RoundedCornerShape(TaffyRadii.sheet), shapes.hero)
    }

    @Test
    fun `the ramp still reads eight ten fourteen eighteen twenty-four`() {
        assertEquals(8.dp, TaffyRadii.chip)
        assertEquals(10.dp, TaffyRadii.row)
        assertEquals(14.dp, TaffyRadii.tile)
        assertEquals(18.dp, TaffyRadii.card)
        assertEquals(24.dp, TaffyRadii.sheet)
    }
}
