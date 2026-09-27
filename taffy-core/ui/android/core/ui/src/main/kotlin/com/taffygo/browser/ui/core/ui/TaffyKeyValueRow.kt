// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * A labelled value: the shape most of the inspector and most of settings is
 * made of. One accessible element per pair, described as "label, value".
 */
@Composable
fun TaffyKeyValueRow(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
) {
    val description = taffyString(R.string.taffy_accessible_pair, label, value)
    Row(
        modifier = modifier
            .fillMaxWidth()
            .semantics(mergeDescendants = true) { contentDescription = description },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Text(
            text = label,
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.weight(1f),
        )
        Text(
            text = value,
            style = TaffyTheme.typography.numeric,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.weight(1f),
        )
    }
}
