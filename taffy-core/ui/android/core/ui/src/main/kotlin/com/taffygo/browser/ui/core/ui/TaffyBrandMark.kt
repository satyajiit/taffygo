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

/**
 * The TaffyGo mark, drawn in the theme's own treatment.
 *
 * The brand ships two treatments of one mark — one drawn for dark surfaces and
 * one for light — because the ribbon's gradient loses its edge against the
 * wrong ground. Which one is right is a property of the theme in scope and not
 * of the system, since the Appearance setting can override the system, so this
 * reads [TaffyTheme.isDark] rather than `isSystemInDarkTheme()`, the same way
 * every other light-or-dark skin in this package does.
 *
 * Both treatments are lossless WebP derived from the committed masters in
 * `brand/png/` by `taffy-core/ui/android/tools/icons/generate_launcher_icons.py`
 * at five densities. Unlike the launcher layers they carry no safe-zone
 * padding: the mark fills its square, so [size] is the size it draws at rather
 * than the size of a box it sits inside.
 *
 * [contentDescription] belongs to the caller. This component has no words of
 * its own — a screen that shows the mark as decoration beside a heading that
 * already names the product passes `null`, and a screen where the mark *is*
 * the naming passes its own externalized string. Inventing one here would put
 * a string in `:core:ui` that half its callers would have to talk over.
 */
@Composable
fun TaffyBrandMark(
    size: Dp,
    modifier: Modifier = Modifier,
    contentDescription: String? = null,
) {
    Image(
        painter = painterResource(
            if (TaffyTheme.isDark) R.drawable.taffy_mark_on_dark else R.drawable.taffy_mark_on_light,
        ),
        contentDescription = contentDescription,
        // The asset is a square canvas with the mark centred in it, so Fit
        // preserves the artwork's own framing at any size the caller asks for.
        contentScale = ContentScale.Fit,
        modifier = modifier.size(size),
    )
}
