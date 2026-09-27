// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * One row of a list: a title, a supporting line, and something on the end.
 *
 * The whole row is one accessible element with one description, because eight
 * separate announcements for one row is how a list becomes unusable with a
 * screen reader.
 *
 * [leading] is for a mark that identifies the row's subject — a provider's
 * brand mark on screen SCR-404, for instance. It is inside the merged
 * semantics like everything else, so it adds a picture and never a second
 * announcement.
 */
@Composable
fun TaffyListRow(
    title: String,
    accessibleDescription: String,
    modifier: Modifier = Modifier,
    supporting: String? = null,
    testTag: String? = null,
    selected: Boolean = false,
    onClick: (() -> Unit)? = null,
    leading: @Composable (() -> Unit)? = null,
    trailing: @Composable (() -> Unit)? = null,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.row)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(
                width = TaffyBorders.standard,
                color = when {
                    selected -> TaffyTheme.colors.textPrimary
                    onClick != null -> TaffyTheme.colors.outline
                    else -> Color.Transparent
                },
                shape = TaffyTheme.shapes.row,
            )
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .padding(TaffyTheme.spacing.snug)
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
            .semantics(mergeDescendants = true) {
                contentDescription = accessibleDescription
                this.selected = selected
            },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        leading?.invoke()
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
            if (supporting != null) {
                Text(
                    text = supporting,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
        trailing?.invoke()
    }
}
