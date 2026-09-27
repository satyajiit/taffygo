// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.tooling.preview.Preview

/**
 * The default text size and twice it.
 *
 * Parity row PAR-A11Y-003 says nothing critical clips at the supported maximum
 * scale, and the screen catalog asks for a screenshot at two scales. A screen
 * that only fits at the default one fails here rather than on someone's device.
 */
@Preview(name = "text 100 percent", showBackground = true, fontScale = 1.0f)
@Preview(name = "text 200 percent", showBackground = true, fontScale = 2.0f, heightDp = 1200)
annotation class FontScalePreviews
