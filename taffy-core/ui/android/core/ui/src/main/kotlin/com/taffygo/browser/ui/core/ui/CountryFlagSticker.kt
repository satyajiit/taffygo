// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * A country's flag as a list or chip element: [CountryFlagImage] on a ground,
 * with a border that says whether this is the chosen one.
 *
 * It lives in `:core:ui` rather than in `:feature:onboarding` because SCR-407
 * wants the same element as SCR-006 and SCR-001, and features never depend on
 * features.
 *
 * ## The tilt is gone
 *
 * This used to draw at -2°, which reads as a sticker when the content is an
 * emoji glyph on a rounded square. Once the content is a rectangle with a
 * hairline border it stops reading as a sticker and starts reading as a
 * rendering fault — and a rotated box needs more room than its own size, which
 * a 34 dp chip does not have.
 */
@Composable
fun CountryFlagSticker(
    code: String,
    width: Dp,
    modifier: Modifier = Modifier,
    selected: Boolean = false,
) {
    Box(
        modifier = modifier
            .clip(TaffyTheme.shapes.chip)
            .background(
                if (selected) TaffyTheme.colors.accentWash else TaffyTheme.colors.surfaceRaised,
            )
            .border(
                TaffyBorders.standard,
                if (selected) TaffyTheme.colors.accent else TaffyTheme.colors.outline,
                TaffyTheme.shapes.chip,
            ),
        contentAlignment = Alignment.Center,
    ) {
        CountryFlagImage(code = code, width = width)
    }
}
