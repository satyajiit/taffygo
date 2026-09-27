// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyEdges

/**
 * How far the keyboard reaches past the row at the bottom of the window, and
 * nothing when it does not reach it at all.
 *
 * Put this height in a bottom-anchored column, one line above that row: height
 * added there pushes everything above it up and leaves the row where it is.
 * That is the whole trick, and it is why the row takes
 * [TaffyEdges.bottomBar][com.taffygo.browser.ui.core.designsystem.TaffyEdges]
 * rather than the inset that includes the keyboard — the row stays at the
 * bottom of the window with the keyboard drawn over it, and what has to clear
 * the keyboard is lifted instead of the whole column moving.
 *
 * It costs nothing to look at, either. The gap it opens runs from the top of
 * the row to the top of the keyboard, which is inside the keyboard's own
 * rectangle, so the keyboard is drawn over it and nobody ever sees the surface
 * behind it.
 *
 * **[rowHeightPx] is measured inside the navigation-bar inset**, the way
 * `ChromeRow` is, or the bar is counted twice and the lift overshoots by its
 * height. It is a parameter because the two start-page hosts put different
 * things at the bottom: screen SCR-101's chrome column carries a top border and
 * a step of padding the four-slot dock on SCR-102 does not, so the same rule
 * over two different rows is the reason this is one function rather than two
 * that agree until one of them is edited.
 */
@Composable
internal fun keyboardLiftAbove(rowHeightPx: Int): Dp {
    val density = LocalDensity.current
    return with(density) {
        val keyboard = TaffyEdges.keyboard.getBottom(density)
        val row = TaffyEdges.bottomBar.getBottom(density) + rowHeightPx
        (keyboard - row).coerceAtLeast(0).toDp()
    }
}
