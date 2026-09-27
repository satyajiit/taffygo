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

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffyButtonsLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyButtonsPreviewContent()
    }
}

@ThemePreviews
@Composable
private fun TaffyButtonsDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyButtonsPreviewContent()
    }
}

/** The three variants and the icon-only square, as the design document rows them. */
@Composable
private fun TaffyButtonsPreviewContent() {
    Row(
        modifier = Modifier.padding(TaffyTheme.spacing.tight),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffyPrimaryButton(
            label = taffyString(R.string.taffy_control_take_over),
            onClick = {},
            icon = TaffyIcon.Hand,
            size = TaffyButtonSize.COMPACT,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_control_pause),
            onClick = {},
            size = TaffyButtonSize.COMPACT,
        )
        TaffyDangerButton(
            label = taffyString(R.string.taffy_control_stop),
            onClick = {},
            size = TaffyButtonSize.COMPACT,
        )
        TaffyIconButton(
            icon = TaffyIcon.ArrowLeft,
            contentDescription = taffyString(R.string.taffy_action_back),
            onClick = {},
            size = TaffyButtonSize.COMPACT,
        )
    }
}
