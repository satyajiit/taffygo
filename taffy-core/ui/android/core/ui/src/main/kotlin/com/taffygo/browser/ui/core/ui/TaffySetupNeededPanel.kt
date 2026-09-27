// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The one panel every AI surface shows when Taffy has no provider to reach.
 *
 * The Ask sheet draws it in place of its composer, the start page's box in
 * place of its start row, and a failed task beneath its reason line, so the words a
 * person meets are the same wherever they arrived from. It is
 * [TaffyEmptyState] with the mark, a full-width primary action that opens
 * AI & providers, and an optional way out. The caller owns every string,
 * because which sentence is true depends on what it knows — nothing set up,
 * or a route chosen with nothing behind it — and this module knows neither.
 *
 * About 260 dp at normal text, so it fits under a dock sheet's half-height
 * peek without scrolling.
 */
@Composable
fun TaffySetupNeededPanel(
    title: String,
    body: String,
    primaryLabel: String,
    onPrimary: () -> Unit,
    modifier: Modifier = Modifier,
    secondaryLabel: String? = null,
    onSecondary: (() -> Unit)? = null,
    testTag: String = TAFFY_SETUP_NEEDED_PANEL_TEST_TAG,
    primaryTestTag: String = TAFFY_SETUP_NEEDED_PRIMARY_TEST_TAG,
    secondaryTestTag: String = TAFFY_SETUP_NEEDED_SECONDARY_TEST_TAG,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .testTag(testTag),
    ) {
        TaffyEmptyState(
            title = title,
            body = body,
            illustration = {
                TaffyBrandMark(size = SetupMarkSize, contentDescription = null)
            },
            action = {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = TaffyTheme.spacing.tight),
                    verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                ) {
                    TaffyPrimaryButton(
                        label = primaryLabel,
                        onClick = onPrimary,
                        modifier = Modifier.fillMaxWidth(),
                        testTag = primaryTestTag,
                    )
                    if (secondaryLabel != null && onSecondary != null) {
                        TaffySecondaryButton(
                            label = secondaryLabel,
                            onClick = onSecondary,
                            modifier = Modifier.fillMaxWidth(),
                            testTag = secondaryTestTag,
                        )
                    }
                }
            },
        )
    }
}

/** The panel's container. */
const val TAFFY_SETUP_NEEDED_PANEL_TEST_TAG: String = "taffy_setup_needed"

/** The action that opens AI & providers. */
const val TAFFY_SETUP_NEEDED_PRIMARY_TEST_TAG: String = "taffy_setup_needed_primary"

/** The way out, when the caller offers one. */
const val TAFFY_SETUP_NEEDED_SECONDARY_TEST_TAG: String = "taffy_setup_needed_secondary"

private val SetupMarkSize = 56.dp
