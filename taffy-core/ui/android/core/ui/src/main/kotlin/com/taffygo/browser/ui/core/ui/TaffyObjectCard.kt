// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * Hairline object card, radius 18. Colour lives in the icon, not the frame —
 * unless the card stands for a hub group, when [wash] lays that group's
 * ground over the raised surface (decision 0103).
 */
@Composable
fun TaffyObjectCard(
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    wash: TaffyTileWash = TaffyTileWash.Neutral,
    testTag: String? = null,
    content: @Composable BoxScope.() -> Unit,
) {
    val shape = TaffyTheme.shapes.card
    val cardModifier = modifier
        .fillMaxWidth()
        .clip(shape)
        .background(TaffyTheme.colors.surfaceRaised)
        .background(wash.fillColor())
        .border(TaffyBorders.standard, TaffyTheme.colors.outline, shape)
        .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
    val padded = Modifier.padding(TaffyTheme.spacing.cardPadding)
    if (onClick != null) {
        TaffyPressable(onClick = onClick, modifier = cardModifier) {
            Box(modifier = padded, content = content)
        }
    } else {
        Box(modifier = cardModifier.then(padded), content = content)
    }
}
