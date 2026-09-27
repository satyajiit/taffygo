// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.tooling.preview.Preview

/**
 * The handoff's tablet frame (1024 × 768), so a screen that changes at
 * expanded width has a preview that shows the two-pane layout.
 */
@Preview(
    name = "tablet",
    widthDp = 1024,
    heightDp = 768,
    showBackground = true,
    backgroundColor = 0xFFFBF8F3,
)
annotation class TabletPreviews
