// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One Memory suggestion after a finished task. Outline, never amber fill.
 * At most one card; the task view decides whether to draw it.
 */
@Composable
fun RememberThisCard(
    suggestion: RememberThisSuggestion,
    onRemember: () -> Unit,
    onNotNow: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.snug)
            .testTag(REMEMBER_THIS_CARD_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = taffyString(R.string.taffy_remember_this_title),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.semantics { heading() },
        )
        Text(
            text = suggestion.statement,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.testTag(REMEMBER_THIS_STATEMENT_TEST_TAG),
        )
        Text(
            text = suggestion.why,
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_remember_this_remember),
                onClick = onRemember,
                testTag = REMEMBER_THIS_REMEMBER_TEST_TAG,
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_remember_this_not_now),
                onClick = onNotNow,
                testTag = REMEMBER_THIS_NOT_NOW_TEST_TAG,
            )
        }
    }
}

/** The tags the Remember this card's tests name. */
const val REMEMBER_THIS_CARD_TEST_TAG: String = "remember_this_card"
const val REMEMBER_THIS_STATEMENT_TEST_TAG: String = "remember_this_statement"
const val REMEMBER_THIS_REMEMBER_TEST_TAG: String = "remember_this_remember"
const val REMEMBER_THIS_NOT_NOW_TEST_TAG: String = "remember_this_not_now"
