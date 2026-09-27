// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The one switch. Replaces a raw Material [Switch] so every settings row
 * uses Taffy tokens and carries a name.
 *
 * [accessibleName] describes the purpose, not the on/off state — the control
 * already announces that. The track is ink when on, never accent: amber is
 * for Taffy working, not for a setting.
 */
@Composable
fun TaffySwitch(
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    accessibleName: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    testTag: String? = null,
) {
    val colors = TaffyTheme.colors
    Switch(
        checked = checked,
        onCheckedChange = onCheckedChange,
        enabled = enabled,
        modifier = modifier
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
            .semantics { contentDescription = accessibleName },
        colors = SwitchDefaults.colors(
            checkedThumbColor = colors.surfaceRaised,
            checkedTrackColor = colors.textPrimary,
            checkedBorderColor = colors.textPrimary,
            uncheckedThumbColor = colors.surfaceRaised,
            uncheckedTrackColor = colors.hairline,
            uncheckedBorderColor = colors.hairline,
            disabledCheckedThumbColor = colors.surfaceRaised,
            disabledCheckedTrackColor = colors.textPrimary.copy(alpha = 0.38f),
            disabledUncheckedThumbColor = colors.surfaceRaised,
            disabledUncheckedTrackColor = colors.hairline.copy(alpha = 0.38f),
        ),
    )
}
