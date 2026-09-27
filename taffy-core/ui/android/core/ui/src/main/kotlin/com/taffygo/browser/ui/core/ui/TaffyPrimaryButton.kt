// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.internal.TaffyButtonBase

/**
 * The one action a screen most wants the user to take.
 *
 * Primary is ink on paper and paper on ink — symmetric, and the only neutral
 * fill in the UI (`handoff/TgButton.dc.html`). Amber is never an action.
 */
@Composable
fun TaffyPrimaryButton(
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
        variant = TaffyButtonVariant.PRIMARY,
        size = size,
        modifier = modifier,
        icon = icon,
        enabled = enabled,
        loading = loading,
        testTag = testTag,
    )
}
