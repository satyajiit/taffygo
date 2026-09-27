// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffyGroupedCardLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyGroupedCardPreviewBody()
    }
}

@ThemePreviews
@Composable
private fun TaffyGroupedCardDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyGroupedCardPreviewBody()
    }
}

@Composable
private fun TaffyGroupedCardPreviewBody() {
    TaffyGroupedCard {
        PreviewRow(
            title = taffyString(R.string.taffy_mode_you_browse),
            summary = taffyString(R.string.taffy_action_back),
            icon = TaffyIcon.UserCircle,
        )
        TaffyGroupedCardDivider()
        PreviewRow(
            title = taffyString(R.string.taffy_assistant_ask_taffy),
            summary = taffyString(R.string.taffy_control_take_over),
            icon = TaffyIcon.Sparkle,
        )
    }
}

@Composable
private fun PreviewRow(title: String, summary: String, icon: ImageVector) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 72.dp)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            ),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TaffyGlyphFrame {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(20.dp),
            )
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = summary,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}
