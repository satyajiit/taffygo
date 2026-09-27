// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * Borderless help or empty tile. Recedes behind object cards.
 */
@Composable
fun TaffyInfoTile(
    modifier: Modifier = Modifier,
    testTag: String? = null,
    content: @Composable BoxScope.() -> Unit,
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceSunken)
            .padding(TaffyTheme.spacing.cardPadding)
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
        content = content,
    )
}
