// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The packing behind [TaffyBentoGrid]: rows fill in order, a span is clamped
 * to the grid, and the last tile in a row takes the cells nothing claimed.
 */
class TaffyBentoGridTest {

    @Test
    fun `three single tiles on two columns are a pair and a stretched third`() {
        assertEquals(
            listOf(
                listOf(TaffyBentoCell(0, 1), TaffyBentoCell(1, 1)),
                listOf(TaffyBentoCell(2, 2)),
            ),
            packTaffyBentoRows(listOf(1, 1, 1), columns = 2),
        )
    }

    @Test
    fun `a full tile is the row on any grid`() {
        assertEquals(
            listOf(
                listOf(TaffyBentoCell(0, 2)),
                listOf(TaffyBentoCell(1, 1), TaffyBentoCell(2, 1)),
            ),
            packTaffyBentoRows(listOf(TaffyBentoSpan.Full.cells, 1, 1), columns = 2),
        )
        assertEquals(
            listOf(listOf(TaffyBentoCell(0, 4))),
            packTaffyBentoRows(listOf(TaffyBentoSpan.Full.cells), columns = 4),
        )
    }

    @Test
    fun `a tile that does not fit starts the next row`() {
        assertEquals(
            listOf(
                listOf(TaffyBentoCell(0, 2)),
                listOf(TaffyBentoCell(1, 2)),
                listOf(TaffyBentoCell(2, 2)),
            ),
            packTaffyBentoRows(listOf(1, 2, 1), columns = 2),
        )
    }

    @Test
    fun `a folded grid is one tile per row`() {
        assertEquals(
            listOf(
                listOf(TaffyBentoCell(0, 1)),
                listOf(TaffyBentoCell(1, 1)),
                listOf(TaffyBentoCell(2, 1)),
            ),
            packTaffyBentoRows(listOf(1, 2, TaffyBentoSpan.Full.cells), columns = 1),
        )
    }

    @Test
    fun `on four columns the last tile takes what is left`() {
        assertEquals(
            listOf(listOf(TaffyBentoCell(0, 1), TaffyBentoCell(1, 1), TaffyBentoCell(2, 2))),
            packTaffyBentoRows(listOf(1, 1, 1), columns = 4),
        )
    }

    @Test
    fun `nothing packs to nothing`() {
        assertEquals(emptyList<List<TaffyBentoCell>>(), packTaffyBentoRows(emptyList(), columns = 2))
    }
}
