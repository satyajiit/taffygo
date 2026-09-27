// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * Two stacked full-width children.
 *
 * A side-by-side pair of destination tiles clipped titles on the phone.
 * Settings, stats, help, You chapters, and the start page must not draw a
 * 2-column grid; this is always a column.
 */
@Composable
fun TaffyTwoUp(
    first: @Composable () -> Unit,
    second: @Composable () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        first()
        second()
    }
}
