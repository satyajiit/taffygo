// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.widthIn
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The bottom chrome's width: full on a phone, capped and left-aligned on a
 * tablet (`handoff/DESIGN.md` section 9).
 */
@Composable
fun Modifier.taffyChromeWidth(): Modifier {
    val maxWidth = TaffyTheme.spacing.chromeMaxWidth
    return if (maxWidth == Dp.Unspecified) {
        fillMaxWidth()
    } else {
        fillMaxWidth().widthIn(max = maxWidth)
    }
}
