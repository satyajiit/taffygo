// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The permanent data boundary, reduced to the one sentence needed here.
 *
 * Bordered rather than bare. It used to be a glyph and a caption floating
 * under the last card, which is the shape of a footnote — something the screen
 * mentions on its way out. This sentence is not a footnote: it is the one
 * promise on SCR-004 that holds whichever route is chosen, and it is a
 * permanent invariant rather than a setting. Giving it the same hairline
 * outline the routes above it wear says it is of the same kind as they are,
 * and giving it no fill says it is not a fourth thing to choose.
 */
@Composable
internal fun AiSetupAssurance(modifier: Modifier = Modifier) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.snug)
            .testTag(AI_SETUP_FOOTNOTE_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.ShieldCheck,
            contentDescription = null,
            tint = TaffyTheme.colors.textPrimary,
            modifier = Modifier.size(ShieldSize),
        )
        Text(
            text = taffyString(R.string.taffy_ai_setup_assure_compact),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.weight(1f),
        )
    }
}

private val ShieldSize = 20.dp
