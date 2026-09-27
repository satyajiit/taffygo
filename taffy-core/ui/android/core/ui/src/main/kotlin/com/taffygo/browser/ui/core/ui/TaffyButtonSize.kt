// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * The three heights an action button renders at (`handoff/TgButton.dc.html`).
 *
 * Each size carries its spec geometry with it — horizontal padding, label
 * size, and icon sizes all step with the height — so a caller picks a size and
 * nothing else. Every size clears the forty-four-unit touch floor; the default
 * is the forty-eight-unit step the handoff asks primary CTAs to use.
 */
enum class TaffyButtonSize(
    internal val height: Dp,
    internal val horizontalPadding: Dp,
    internal val labelSize: TextUnit,
    internal val iconWithLabelSize: Dp,
    internal val iconOnlySize: Dp,
) {
    /** The compact step: rows and sheets. */
    COMPACT(44.dp, 18.dp, 13.sp, 15.dp, 20.dp),

    /** The default step: primary CTAs. */
    DEFAULT(48.dp, 20.dp, 13.5.sp, 15.dp, 21.dp),

    /** The large step: the rare hero action. */
    LARGE(52.dp, 20.dp, 14.sp, 15.dp, 21.dp),
}
