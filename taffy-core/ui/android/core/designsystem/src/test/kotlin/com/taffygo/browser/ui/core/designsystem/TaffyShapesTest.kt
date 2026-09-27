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

/** Soft Pulse radii: 8 / 10 / 14 / 18 / 24. */
class TaffyShapesTest {

    @Test
    fun `the ramp is ranked rather than scattered`() {
        val shapes = TaffyShapes.Default

        assertEquals(RoundedCornerShape(8.dp), shapes.chip)
        assertEquals(RoundedCornerShape(10.dp), shapes.row)
        assertEquals(RoundedCornerShape(14.dp), shapes.tile)
        assertEquals(RoundedCornerShape(18.dp), shapes.card)
        assertEquals(RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp), shapes.sheet)
        assertEquals(RoundedCornerShape(24.dp), shapes.hero)
        assertEquals(RoundedCornerShape(percent = 50), shapes.pill)
        assertEquals(RoundedCornerShape(percent = 50), shapes.button)
        assertEquals(RoundedCornerShape(percent = 50), shapes.switch)
    }
}
