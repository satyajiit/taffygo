// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * The Material 3 window-width buckets, named the way the handoff talks about
 * them: compact is a phone, expanded is a tablet, and medium is the fold and
 * the large phone in landscape.
 *
 * Breakpoints are the platform's (`600` / `840`), not a private set. A screen
 * that branched on raw density-independent pixels would invent a fourth width
 * the first time two authors picked different numbers.
 */
enum class TaffyWindowWidth {

    /** Narrower than [MEDIUM_MIN_DP]: one pane, phone tokens. */
    COMPACT,

    /** From [MEDIUM_MIN_DP] up to [EXPANDED_MIN_DP]: two panes, phone type. */
    MEDIUM,

    /** [EXPANDED_MIN_DP] and up: two panes, the tablet type and gutter. */
    EXPANDED;

    /** Whether a list and its detail sit side by side. */
    val showsTwoPane: Boolean
        get() = this != COMPACT

    /** Whether the display and body roles take the tablet step up. */
    val usesTabletType: Boolean
        get() = this == EXPANDED

    companion object {
        /** The Material 3 compact/medium boundary, in density-independent pixels. */
        const val MEDIUM_MIN_DP: Int = 600

        /** The Material 3 medium/expanded boundary, in density-independent pixels. */
        const val EXPANDED_MIN_DP: Int = 840

        /** The bucket [widthDp] falls into. */
        fun fromWidthDp(widthDp: Int): TaffyWindowWidth = when {
            widthDp >= EXPANDED_MIN_DP -> EXPANDED
            widthDp >= MEDIUM_MIN_DP -> MEDIUM
            else -> COMPACT
        }

        /** The bucket [width] falls into. */
        fun fromWidth(width: Dp): TaffyWindowWidth = fromWidthDp(width.value.toInt())
    }
}

/**
 * The width of the window this composition is drawn in.
 *
 * Reads the container size, so multi-window and an inner foldable display
 * get the size of this window rather than the device. An unmeasured
 * container (a first frame, some previews) is compact.
 */
@Composable
fun currentTaffyWindowWidth(): TaffyWindowWidth {
    val containerWidth = LocalWindowInfo.current.containerSize.width
    if (containerWidth <= 0) return TaffyWindowWidth.COMPACT
    val width = with(LocalDensity.current) { containerWidth.toDp() }
    return TaffyWindowWidth.fromWidth(width)
}

/** A named width for tests and previews that must not read the window. */
val PhoneWindowWidth: Dp = 360.dp

/** The handoff's tablet frame (`handoff/DESIGN.md` section 8). */
val TabletWindowWidth: Dp = 1024.dp
