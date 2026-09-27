// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrameSize
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider

/**
 * Record-shaped skeletons: a 40 dp disc and two lines, first one named.
 */
@Composable
internal fun PagesRecordSkeleton(
    rows: Int,
    firstDescription: String,
    rowHeight: Dp,
    testTag: String,
    modifier: Modifier = Modifier,
) {
    TaffyGroupedCard(modifier = modifier.testTag(testTag)) {
        repeat(rows) { index ->
            if (index > 0) TaffyGroupedCardDivider()
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(rowHeight)
                    .padding(horizontal = TaffyTheme.spacing.screenMargin),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                TaffySkeleton(
                    modifier = Modifier.size(TaffyGlyphFrameSize),
                    shape = TaffyTheme.shapes.chip,
                    accessibleDescription = if (index == 0) firstDescription else null,
                )
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                ) {
                    TaffySkeleton(
                        modifier = Modifier
                            .fillMaxWidth(TitleFraction)
                            .height(TitleBarHeight),
                        shape = TaffyTheme.shapes.row,
                    )
                    TaffySkeleton(
                        modifier = Modifier
                            .fillMaxWidth(CaptionFraction)
                            .height(CaptionBarHeight),
                        shape = TaffyTheme.shapes.row,
                    )
                }
            }
        }
    }
}

private const val TitleFraction = 0.6f
private const val CaptionFraction = 0.4f
private val TitleBarHeight = 12.dp
private val CaptionBarHeight = 10.dp
