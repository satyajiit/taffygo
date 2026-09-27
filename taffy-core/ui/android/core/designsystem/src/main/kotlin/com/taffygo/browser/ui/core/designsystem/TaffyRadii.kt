// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * The Soft Pulse corner radii, as numbers.
 *
 * [TaffyShapes] is built from these, and so is every draw that outlines a
 * shape by hand. That is the whole reason they exist separately: a `Shape`
 * cannot be asked for its radius, so a `drawRoundRect` tracing a clipped
 * surface has to restate the number — and a restated number drifts. It did.
 * `TabSwitcherScreen` carried a `RowCornerRadius = 16.dp` whose own comment
 * said it restated [TaffyShapes.row], which is 10, so the tab switcher's
 * dashed workspace offer was outlined six density-independent pixels outside
 * the shape clipping it. Read the token; do not copy the figure.
 */
object TaffyRadii {
    /** Chips, tags, and the 26 dp stat glyph host. */
    val chip: Dp = 8.dp

    /** List rows and pressable rows. */
    val row: Dp = 10.dp

    /** Icon tiles, inputs, and nested controls. */
    val tile: Dp = 14.dp

    /** Object cards. */
    val card: Dp = 18.dp

    /** Sheets (top corners) and hero panels. */
    val sheet: Dp = 24.dp
}
