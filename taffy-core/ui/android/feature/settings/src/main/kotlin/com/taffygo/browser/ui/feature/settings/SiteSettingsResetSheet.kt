// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Confirmation boundary for removing one host's saved permission choices. */
@Composable
internal fun SiteSettingsResetSheet(
    host: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_site_settings_reset_title),
        onDismissRequest = onDismiss,
        testTag = SITE_SETTINGS_RESET_SHEET_TEST_TAG,
    ) {
        Text(
            text = taffyString(R.string.taffy_site_settings_reset_body, host),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyDangerButton(
            label = taffyString(R.string.taffy_site_settings_reset_confirm),
            onClick = onConfirm,
            testTag = SITE_SETTINGS_RESET_CONFIRM_TEST_TAG,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_site_settings_reset_cancel),
            onClick = onDismiss,
        )
    }
}
