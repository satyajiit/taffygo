// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.ui.unit.dp

/**
 * The border widths of the design handoff.
 *
 * Fixed values rather than theme tokens: a stroke does not change with the
 * theme, only its colour does (`TaffyTheme.colors.outline` for the ordinary
 * handoff stroke, with semantic colours reserved for selected/error states).
 */
object TaffyBorders {
    /** The default component boundary. */
    val standard = 1.dp

    /** A boundary that must read above its neighbours (provenance rings). */
    val emphasis = 1.5.dp

    /** The progress rail on the assistant pill. */
    val rail = 2.dp
}
