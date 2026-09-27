// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * A source chip: where one fact came from, one tap from the page it came from
 * (UX spec section 6).
 *
 * A confident answer without a source is a defect, so this is the component
 * that appears beside every fact rather than an optional decoration.
 */
@Composable
fun TaffySourceChip(
    host: String,
    accessibleDescription: String,
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
) {
    Text(
        text = host,
        style = TaffyTheme.typography.caption,
        color = TaffyTheme.colors.textPrimary,
        modifier = modifier
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.sourceChip)
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .padding(horizontal = TaffyTheme.spacing.snug, vertical = TaffyTheme.spacing.tight)
            .semantics { contentDescription = accessibleDescription },
    )
}
