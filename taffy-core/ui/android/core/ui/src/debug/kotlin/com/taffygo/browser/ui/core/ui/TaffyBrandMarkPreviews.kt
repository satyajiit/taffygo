// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Debug-only Compose previews; never part of the product APK. */
private val WelcomeSize = 68.dp
private val StepSize = 42.dp

@ThemePreviews
@Composable
private fun TaffyBrandMarkLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyBrandMarkPreviewContent()
    }
}

@ThemePreviews
@Composable
private fun TaffyBrandMarkDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyBrandMarkPreviewContent()
    }
}

@Composable
private fun TaffyBrandMarkPreviewContent() {
    Row(
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.padding(TaffyTheme.spacing.tight),
    ) {
        TaffyBrandMark(size = WelcomeSize)
        TaffyBrandMark(size = StepSize)
    }
}
