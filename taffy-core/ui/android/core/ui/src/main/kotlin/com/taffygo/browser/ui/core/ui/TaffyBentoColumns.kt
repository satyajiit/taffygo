// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyWindowWidth

/**
 * How many columns a bento grid draws in the width it was given.
 *
 * The one home of the fold rule (decision 0103 section 4). A grid is two
 * columns on a compact width, three on a medium one and four on an expanded
 * one — the platform's own breakpoints, read from the width the grid actually
 * has rather than the window, so a list pane on a tablet draws two columns
 * while a full-width hub draws four. When the font scale is [FOLD_FONT_SCALE]
 * or more, or the width is under [FOLD_WIDTH_DP], every grid is one column:
 * that is the rule the hero card has always stacked by, and the two share it
 * here so they cannot drift apart. An earlier two-column settings grid clipped
 * its titles at large text; a grid that folds does not.
 */
object TaffyBentoColumns {

    /** Below this available width the grid is one column. */
    const val FOLD_WIDTH_DP: Int = 340

    /** At or above this font scale the grid is one column. */
    const val FOLD_FONT_SCALE: Float = 1.3f

    /** Columns on a compact width. */
    const val COMPACT_COLUMNS: Int = 2

    /** Columns on a medium width. */
    const val MEDIUM_COLUMNS: Int = 3

    /** Columns on an expanded width. */
    const val EXPANDED_COLUMNS: Int = 4

    /** Whether tiles and hero copy stack: the rule every grid and the hero share. */
    fun folds(availableWidthDp: Int, fontScale: Float): Boolean =
        availableWidthDp < FOLD_WIDTH_DP || fontScale >= FOLD_FONT_SCALE

    /** One when folded; else the count the bucket of [availableWidthDp] earns. */
    fun of(availableWidthDp: Int, fontScale: Float): Int = when {
        folds(availableWidthDp, fontScale) -> 1
        else -> when (TaffyWindowWidth.fromWidthDp(availableWidthDp)) {
            TaffyWindowWidth.COMPACT -> COMPACT_COLUMNS
            TaffyWindowWidth.MEDIUM -> MEDIUM_COLUMNS
            TaffyWindowWidth.EXPANDED -> EXPANDED_COLUMNS
        }
    }
}

/** The column count for [availableWidth] under the font scale in scope. */
@Composable
fun rememberTaffyBentoColumns(availableWidth: Dp): Int {
    val fontScale = LocalDensity.current.fontScale
    return remember(availableWidth, fontScale) {
        TaffyBentoColumns.of(availableWidth.value.toInt(), fontScale)
    }
}
