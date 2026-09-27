// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.graphics.ImageBitmap

/**
 * One page offered to Taffy: title, host, and whether the person can take it
 * off the list.
 *
 * [mark] is a local favicon. Absent, the chip draws the host's initial on a
 * sunken disc — never a network fetch, never the start-page ribbon.
 */
data class TaffyPageChipState(
    val title: String,
    val host: String,
    val mark: ImageBitmap? = null,
    val closed: Boolean = false,
    val taffyOpened: Boolean = false,
    val removable: Boolean = true,
)
