// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * Rounded tiles of mixed span on a column grid, for a [TaffyScreen] body.
 *
 * Columns come from [rememberTaffyBentoColumns] over this composable's own
 * width, so a tablet pane and a phone each get the count their width earns and
 * large text folds everything to one column. Tiles are laid row by row in the
 * order they were added; a tile that does not fit the cells left in a row
 * starts the next one, and the last tile in a row stretches over whatever
 * cells remain, so every row is a rectangle. Tiles in a row share a height.
 *
 * This is an eager layout. A collection whose size is not a small compile-time
 * constant goes through [TaffyLazyGridScreen] and [taffyBentoItems] instead,
 * and this grid may sit inside one of those as a single full-span item.
 */
@Composable
fun TaffyBentoGrid(
    modifier: Modifier = Modifier,
    testTag: String? = null,
    gap: Dp = TaffyTheme.spacing.snug,
    content: TaffyBentoScope.() -> Unit,
) {
    val entries = TaffyBentoEntries().apply(content).entries
    BoxWithConstraints(
        modifier = modifier
            .fillMaxWidth()
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
    ) {
        val columns = rememberTaffyBentoColumns(maxWidth)
        val rows = packTaffyBentoRows(entries.map { it.span.cells }, columns)
        Column(verticalArrangement = Arrangement.spacedBy(gap)) {
            for (row in rows) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(IntrinsicSize.Max),
                    horizontalArrangement = Arrangement.spacedBy(gap),
                ) {
                    for (cell in row) {
                        Box(
                            modifier = Modifier
                                .weight(cell.cells.toFloat())
                                .fillMaxHeight(),
                            propagateMinConstraints = true,
                        ) {
                            entries[cell.index].content()
                        }
                    }
                }
            }
        }
    }
}

/** One tile's place in a packed row: which entry, and how many cells it takes. */
internal data class TaffyBentoCell(val index: Int, val cells: Int)

/**
 * Packs spans into rows of [columns] cells, greedily and in order.
 *
 * Pure, so a host test can hold the rule: a span is clamped to the grid, a
 * tile that does not fit starts a new row, and the last tile in each row is
 * widened over the cells nothing claimed.
 */
internal fun packTaffyBentoRows(spans: List<Int>, columns: Int): List<List<TaffyBentoCell>> {
    require(columns >= 1) { "a grid has at least one column" }
    val rows = mutableListOf<MutableList<TaffyBentoCell>>()
    var remaining = 0
    spans.forEachIndexed { index, span ->
        val cells = span.coerceIn(1, columns)
        if (cells > remaining) {
            rows.add(mutableListOf())
            remaining = columns
        }
        rows.last().add(TaffyBentoCell(index, cells))
        remaining -= cells
    }
    return rows.map { row ->
        val used = row.sumOf { it.cells }
        if (used == columns) {
            row
        } else {
            val last = row.last()
            row.dropLast(1) + last.copy(cells = last.cells + columns - used)
        }
    }
}

private class TaffyBentoEntry(val span: TaffyBentoSpan, val content: @Composable () -> Unit)

private class TaffyBentoEntries : TaffyBentoScope {
    val entries = mutableListOf<TaffyBentoEntry>()

    override fun item(span: TaffyBentoSpan, content: @Composable () -> Unit) {
        entries += TaffyBentoEntry(span, content)
    }
}
