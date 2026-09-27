// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The screen title, the way back, and an optional toolbar action.
 *
 * OpenAlly ScreenHeader: row 1 is Back (chevron + the word) on the leading
 * edge and any CTA on the trailing edge; row 2 is the title. Back is sized to
 * its chip and uses opacity, not a Material ripple — a Column child would
 * otherwise fill the width and flash the whole bar.
 */
@Composable
fun TaffyTopBar(
    title: String,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    onBack: (() -> Unit)? = null,
    toolbarRight: (@Composable () -> Unit)? = null,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = TaffyTheme.spacing.screenMargin),
    ) {
        if (onBack != null || toolbarRight != null) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Bottom,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                if (onBack != null) {
                    BackChip(onBack = onBack)
                } else {
                    Box(Modifier.weight(1f))
                }
                toolbarRight?.invoke()
            }
        }
        Column(
            modifier = Modifier.padding(
                top = if (onBack != null || toolbarRight != null) {
                    0.dp
                } else {
                    TaffyTheme.spacing.tight
                },
                bottom = TaffyTheme.spacing.tight,
            ),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.headline,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.semantics { heading() },
            )
            if (subtitle != null) {
                Text(
                    text = subtitle,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
}

@Composable
private fun BackChip(onBack: () -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val back = taffyString(R.string.taffy_action_back)
    Row(
        modifier = Modifier
            .wrapContentWidth(Alignment.Start)
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .graphicsLayer { alpha = if (pressed) BackPressedAlpha else 1f }
            .clickable(
                interactionSource = interaction,
                indication = null,
                role = Role.Button,
                onClick = onBack,
            )
            .padding(end = TaffyTheme.spacing.snug)
            .testTag(BACK_TEST_TAG)
            .semantics(mergeDescendants = true) { contentDescription = back },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
    ) {
        Icon(
            imageVector = TaffyIcon.CaretLeft,
            contentDescription = null,
            tint = TaffyTheme.colors.textPrimary,
            modifier = Modifier.size(BackChevronSize),
        )
        Text(
            text = back,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
    }
}

/** The tag the way back carries, so a semantics test names it once. */
const val BACK_TEST_TAG: String = "action_back"

private val BackChevronSize = 22.dp
private const val BackPressedAlpha = 0.65f
