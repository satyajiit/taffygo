// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton

/** Confirm a destructive row action before it runs. */
@Composable
internal fun PagesConfirmSheet(
    title: String,
    body: String,
    confirmLabel: String,
    cancelLabel: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
    testTag: String,
) {
    TaffyBottomSheet(
        title = title,
        onDismissRequest = onDismiss,
        testTag = testTag,
    ) {
        Text(
            text = body,
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            TaffySecondaryButton(
                label = cancelLabel,
                onClick = onDismiss,
                testTag = "${testTag}_cancel",
                modifier = Modifier.weight(1f),
            )
            TaffyDangerButton(
                label = confirmLabel,
                onClick = onConfirm,
                testTag = "${testTag}_confirm",
                modifier = Modifier.weight(1f),
            )
        }
    }
}
