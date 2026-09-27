// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon

/**
 * Swipe-to-delete that never stands alone: [onSwiped] opens confirm, and a
 * labelled button on the row must do the same job.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun PagesSwipeDelete(
    enabled: Boolean,
    onSwiped: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable RowScope.() -> Unit,
) {
    val state = rememberSwipeToDismissBoxState()
    LaunchedEffect(state.currentValue, enabled) {
        if (state.currentValue != SwipeToDismissBoxValue.EndToStart) return@LaunchedEffect
        if (enabled) onSwiped()
        // The gesture opens a trusted confirmation; it does not itself delete
        // the row. Return the anchor to settled so dismissing that dialog does
        // not leave a visually deleted item or depend on the deprecated veto.
        state.reset()
    }
    SwipeToDismissBox(
        state = state,
        modifier = modifier,
        enableDismissFromStartToEnd = false,
        enableDismissFromEndToStart = enabled,
        gesturesEnabled = enabled && !TaffyTheme.reducedMotion,
        backgroundContent = {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(TaffyTheme.colors.dangerWash)
                    .padding(end = TaffyTheme.spacing.screenMargin),
                contentAlignment = Alignment.CenterEnd,
            ) {
                Icon(
                    imageVector = TaffyIcon.Trash,
                    contentDescription = null,
                    tint = TaffyTheme.colors.dangerText,
                )
            }
        },
        content = content,
    )
}
