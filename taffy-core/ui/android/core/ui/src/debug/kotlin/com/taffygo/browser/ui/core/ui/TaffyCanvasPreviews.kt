// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffyCanvasLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyCanvasPreviewBody()
    }
}

@ThemePreviews
@Composable
private fun TaffyCanvasDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyCanvasPreviewBody()
    }
}

@Composable
private fun TaffyCanvasPreviewBody() {
    Column(
        modifier = Modifier.padding(TaffyTheme.spacing.screenMargin),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        TaffyTopBar(
            title = taffyString(R.string.taffy_mode_you_browse),
            onBack = {},
        )
        TaffyHeroCard(
            title = taffyString(R.string.taffy_assistant_ask_taffy),
            eyebrow = taffyString(R.string.taffy_mode_taffy_browses),
            body = taffyString(R.string.taffy_control_take_over),
        )
        TaffySectionBar(
            title = taffyString(R.string.taffy_mode_browse_together),
            eyebrow = taffyString(R.string.taffy_assistant_review),
        )
        TaffySectionBar(
            title = taffyString(R.string.taffy_mode_browse_together),
            eyebrow = taffyString(R.string.taffy_assistant_review),
            count = taffyCount(3),
            countDescription = taffyString(R.string.taffy_assistant_review),
        )
        TaffyTwoUp(
            first = {
                TaffyStatTile(
                    value = taffyCount(0),
                    label = taffyString(R.string.taffy_control_pause),
                    icon = TaffyIcon.ChartBar,
                )
            },
            second = {
                TaffyStatTile(
                    value = taffyCount(0),
                    label = taffyString(R.string.taffy_control_stop),
                    icon = TaffyIcon.Funnel,
                )
            },
        )
        TaffyCategoryTile(
            title = taffyString(R.string.taffy_mode_you_browse),
            summary = taffyString(R.string.taffy_control_take_over),
            accessibleDescription = taffyString(
                R.string.taffy_accessible_pair,
                taffyString(R.string.taffy_mode_you_browse),
                taffyString(R.string.taffy_control_take_over),
            ),
            icon = TaffyIcon.UserCircle,
            index = taffyChapterIndex(1),
            wash = TaffyTileWash.Accent,
            chapter = true,
            onClick = {},
        )
        TaffyObjectCard {
            Text(
                text = taffyString(R.string.taffy_assistant_resume),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
        }
        TaffyInfoTile {
            Text(
                text = taffyString(R.string.taffy_search_clear),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        TaffySwitch(
            checked = true,
            onCheckedChange = {},
            accessibleName = taffyString(R.string.taffy_control_pause),
        )
    }
}
