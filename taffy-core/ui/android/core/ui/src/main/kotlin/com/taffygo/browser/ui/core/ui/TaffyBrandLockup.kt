// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** The supplied Taffy mark and `taffyGo` wordmark as one theme-aware image. */
@Composable
fun TaffyBrandLockup(
    height: Dp,
    modifier: Modifier = Modifier,
    contentDescription: String? = null,
) {
    Image(
        painter = painterResource(
            if (TaffyTheme.isDark) {
                R.drawable.taffy_lockup_on_dark
            } else {
                R.drawable.taffy_lockup_on_light
            },
        ),
        contentDescription = contentDescription,
        contentScale = ContentScale.Fit,
        modifier = modifier.then(Modifier.brandLockupSize(height)),
    )
}

/** Keep the committed artwork's 180:52 canvas at every requested height. */
private fun Modifier.brandLockupSize(height: Dp): Modifier =
    this.then(Modifier.size(width = height * LockupAspectRatio, height = height))

private const val LockupAspectRatio = 180f / 52f
