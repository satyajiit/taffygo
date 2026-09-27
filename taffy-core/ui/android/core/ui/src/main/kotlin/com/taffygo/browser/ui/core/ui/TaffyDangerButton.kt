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
 * An action that destroys or refuses something.
 *
 * A tinted wash of the danger hue, never a solid fill
 * (`handoff/TgButton.dc.html`), so a destructive tap can never be taken by
 * muscle memory.
 */
@Composable
fun TaffyDangerButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    loading: Boolean = false,
    testTag: String? = null,
    icon: ImageVector? = null,
    size: TaffyButtonSize = TaffyButtonSize.DEFAULT,
) {
    TaffyButtonBase(
        label = label,
        onClick = onClick,
        variant = TaffyButtonVariant.DANGER,
        size = size,
        modifier = modifier,
        icon = icon,
        enabled = enabled,
        loading = loading,
        testTag = testTag,
    )
}
