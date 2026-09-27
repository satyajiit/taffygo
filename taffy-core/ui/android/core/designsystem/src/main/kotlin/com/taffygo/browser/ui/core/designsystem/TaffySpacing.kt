// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.runtime.Immutable
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * The spacing grid of the screen catalog, section 1.3, with the handoff's
 * tablet gutter (`handoff/DESIGN.md` section 5).
 *
 * Compact keeps the catalog's sixteen-unit phone margin. Medium and expanded
 * step the gutter up rather than stretching the phone numbers; the touch
 * target does not change with width.
 */
@Immutable
data class TaffySpacing(
    /** One step of the grid. */
    val step: Dp = 4.dp,
    /** Two steps: the gap inside a chip or between an icon and its label. */
    val tight: Dp = 8.dp,
    /** Three steps: the gap between rows of a list. */
    val snug: Dp = 12.dp,
    /** The screen gutter. */
    val screenMargin: Dp = 16.dp,
    /** Padding inside a card or tile. Independent of the screen gutter. */
    val cardPadding: Dp = 14.dp,
    /** Six steps on the phone; one step more on the tablet. */
    val section: Dp = 24.dp,
    /** The smallest a control may be in either direction. */
    val minimumTouchTarget: Dp = 48.dp,
    /** The gap between two panes when they sit side by side. */
    val paneGap: Dp = 16.dp,
    /**
     * The widest either end of the chrome may grow. [Dp.Unspecified] means
     * the chrome fills the pane — the phone behaviour. On a tablet the top
     * bar and the action row are both capped at this width and both centre
     * what they cap, so the address pill sits over the middle of the row
     * beneath it rather than against the pane's leading edge.
     */
    val chromeMaxWidth: Dp = Dp.Unspecified,
) {
    companion object {
        /** Compact (phone) tokens. Shared so the theme does not allocate. */
        val Compact: TaffySpacing = TaffySpacing()

        /** Medium tokens: a larger gutter, two-pane gap, capped chrome. */
        val Medium: TaffySpacing = TaffySpacing(
            screenMargin = 20.dp,
            paneGap = 16.dp,
            chromeMaxWidth = 520.dp,
        )

        /** Expanded (tablet) tokens, matching the handoff's 24–26 gutter. */
        val Expanded: TaffySpacing = TaffySpacing(
            screenMargin = 24.dp,
            section = 32.dp,
            paneGap = 22.dp,
            chromeMaxWidth = 560.dp,
        )

        /** The cached grid for [width]. */
        fun forWidth(width: TaffyWindowWidth): TaffySpacing = when (width) {
            TaffyWindowWidth.COMPACT -> Compact
            TaffyWindowWidth.MEDIUM -> Medium
            TaffyWindowWidth.EXPANDED -> Expanded
        }
    }
}
