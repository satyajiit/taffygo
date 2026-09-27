// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The right pane of You on a tablet, before a section is chosen.
 */
@Composable
fun YouPanePlaceholder(modifier: Modifier = Modifier) {
    TaffyEmptyState(
        title = taffyString(R.string.taffy_you_pane_empty_title),
        body = taffyString(R.string.taffy_you_pane_empty_body),
        modifier = modifier.testTag(YOU_PANE_EMPTY_TEST_TAG),
    )
}

/** The tag the empty You pane's semantics tests name. */
const val YOU_PANE_EMPTY_TEST_TAG: String = "you_pane_empty"
