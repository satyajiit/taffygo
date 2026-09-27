// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.tooling.preview.Preview

/**
 * Light and dark, side by side.
 *
 * The screen catalog asks for a screenshot of every surface in both themes, so
 * every screen carries this annotation rather than one preview in whichever
 * theme its author happened to be using.
 */
@Preview(name = "light", showBackground = true, backgroundColor = 0xFFFFFFFF)
@Preview(name = "dark", showBackground = true, backgroundColor = 0xFF0E1013, uiMode = 0x21)
annotation class ThemePreviews
