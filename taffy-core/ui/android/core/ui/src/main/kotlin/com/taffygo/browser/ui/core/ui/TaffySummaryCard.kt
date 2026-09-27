// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * An 88 dp numeric summary tile.
 *
 * [value] is already formatted — this tile never invents a number. [positiveTone]
 * paints only that number in `positiveText`, and only when the caller says the
 * caption already makes the tone true (a blocked count).
 */
@Composable
fun TaffySummaryCard(
    value: String,
    caption: String,
    modifier: Modifier = Modifier,
    supporting: String? = null,
    positiveTone: Boolean = false,
    testTag: String? = null,
    leading: @Composable (() -> Unit)? = null,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = SummaryHeight)
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.screenMargin)
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        leading?.invoke()
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = value,
                style = TaffyTheme.typography.display.copy(
                    fontFeatureSettings = TaffyTheme.typography.numeric.fontFeatureSettings,
                ),
                color = if (positiveTone) {
                    TaffyTheme.colors.positiveText
                } else {
                    TaffyTheme.colors.textPrimary
                },
            )
            Text(
                text = caption,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            if (supporting != null) {
                Text(
                    text = supporting,
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
}

private val SummaryHeight = 88.dp
