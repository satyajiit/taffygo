// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.TaffyWindowWidth

/**
 * The wrapper every preview uses: the theme, the surface, and nothing else.
 *
 * Previews are comparable across changes only if they are deterministic, so
 * nothing inside a preview reads a clock, a random source, or a device setting.
 * Every preview in the UI layer renders a `UiState` value written down in the
 * preview itself.
 *
 * [reducedMotion] is one of those settings, pinned here for the same reason.
 * The default is `false`, which is exactly what `currentTaffyReducedMotion()`
 * already answers under inspection, so no existing preview changes; passing
 * `true` is how a surface's still form gets reviewed at all. Several draw
 * something genuinely different rather than merely holding position — the
 * address pill's loading rail is one — and until now nothing could see them.
 */
@Composable
fun TaffyPreview(
    darkTheme: Boolean,
    windowWidth: TaffyWindowWidth = TaffyWindowWidth.COMPACT,
    reducedMotion: Boolean = false,
    content: @Composable () -> Unit,
) {
    TaffyTheme(
        darkTheme = darkTheme,
        windowWidth = windowWidth,
        reducedMotion = reducedMotion,
        // The same pair the application shell passes, so a theme change
        // reviewed here rises the glyph the phone rises.
        glyphs = TaffyProductThemeGlyphs,
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .background(TaffyTheme.colors.surface),
        ) {
            content()
        }
    }
}
