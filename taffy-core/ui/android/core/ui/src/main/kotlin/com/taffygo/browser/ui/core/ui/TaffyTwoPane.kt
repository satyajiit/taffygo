// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * A compact single pane, or two panes side by side on a wider window.
 *
 * Compact draws only [primary]. Medium and expanded draw both, with the
 * handoff's pane gap between them. A tablet is not a stretched phone
 * (`handoff/DESIGN.md` section 9).
 */
@Composable
fun TaffyTwoPane(
    primary: @Composable () -> Unit,
    secondary: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    split: TaffyPaneSplit = TaffyPaneSplit.LIST_DETAIL,
    enabled: Boolean = TaffyTheme.windowWidth.showsTwoPane,
) {
    if (!enabled) {
        Box(
            modifier = modifier
                .fillMaxSize()
                .testTag(TWO_PANE_SINGLE_TEST_TAG),
        ) {
            primary()
        }
        return
    }
    Row(
        modifier = modifier
            .fillMaxSize()
            .testTag(TWO_PANE_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.paneGap),
    ) {
        Box(
            modifier = Modifier
                .weight(split.primaryWeight)
                .fillMaxHeight()
                .testTag(TWO_PANE_PRIMARY_TEST_TAG),
        ) {
            primary()
        }
        Box(
            modifier = Modifier
                .weight(split.secondaryWeight)
                .fillMaxHeight()
                .testTag(TWO_PANE_SECONDARY_TEST_TAG),
        ) {
            secondary()
        }
    }
}

/** The tags a two-pane layout's semantics tests name. */
const val TWO_PANE_TEST_TAG: String = "taffy_two_pane"
const val TWO_PANE_SINGLE_TEST_TAG: String = "taffy_two_pane_single"
const val TWO_PANE_PRIMARY_TEST_TAG: String = "taffy_two_pane_primary"
const val TWO_PANE_SECONDARY_TEST_TAG: String = "taffy_two_pane_secondary"
