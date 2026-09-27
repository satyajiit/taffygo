// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.ui.internal.TaffyButtonBase

/**
 * An action with no words: a square of the button height with one glyph in it
 * (`handoff/TgButton.dc.html`).
 *
 * The glyph is the whole of what the button says, so the content description
 * is not optional — an icon-only button without one announces nothing.
 */
@Composable
fun TaffyIconButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    variant: TaffyButtonVariant = TaffyButtonVariant.SECONDARY,
    size: TaffyButtonSize = TaffyButtonSize.DEFAULT,
    enabled: Boolean = true,
    testTag: String? = null,
) {
    TaffyButtonBase(
        label = null,
        onClick = onClick,
        variant = variant,
        size = size,
        modifier = modifier,
        icon = icon,
        enabled = enabled,
        contentDescription = contentDescription,
        testTag = testTag,
    )
}
