// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySavedFlowReview
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySavedFlowSceneImage
import com.taffygo.browser.ui.core.ui.taffyString

/** Complete ordered steps precede every manual starting-page navigation. */
@Composable
internal fun SavedFlowRepeatPanel(state: SavedFlowRepeatState, onIntent: (AddressBarIntent) -> Unit) {
    if (!state.visible) return
    Column(
        Modifier.fillMaxWidth().testTag(SAVED_FLOW_REPEAT_TEST_TAG)
            .background(TaffyTheme.colors.ribbonThreeWash, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.snug),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            Text(
                taffyString(when {
                    state.checking -> R.string.taffy_repeat_checking
                    state.reviews.isNotEmpty() -> R.string.taffy_repeat_title
                    else -> R.string.taffy_repeat_unavailable
                }),
                modifier = Modifier.weight(1f),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
            if (state.reviews.isNotEmpty() && LocalDensity.current.fontScale < 1.5f) {
                TaffySavedFlowSceneImage(Modifier.width(88.dp))
            }
        }
        if (state.reviews.isNotEmpty()) Text(
            taffyString(R.string.taffy_repeat_body),
            style = TaffyTheme.typography.body, color = TaffyTheme.colors.textSecondary,
        )
        state.reviews.forEach { review ->
            TaffySavedFlowReview(review)
            TaffyPrimaryButton(
                label = taffyString(if (state.opening) R.string.taffy_repeat_opening else R.string.taffy_repeat_open),
                onClick = { onIntent(AddressBarIntent.OpenSavedFlow(review)) },
                enabled = !state.opening,
                testTag = "$SAVED_FLOW_REPEAT_OPEN_TEST_TAG-${review.id}",
            )
        }
        if (state.failed) Text(
            taffyString(
                if (state.reviews.isEmpty()) R.string.taffy_repeat_lookup_failed
                else R.string.taffy_repeat_failed,
            ),
            style = TaffyTheme.typography.body, color = TaffyTheme.colors.danger,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_repeat_dismiss),
            onClick = { onIntent(AddressBarIntent.DismissSavedFlows) },
            enabled = !state.opening,
        )
    }
}

const val SAVED_FLOW_REPEAT_TEST_TAG: String = "saved_flow_repeat"
const val SAVED_FLOW_REPEAT_OPEN_TEST_TAG: String = "saved_flow_repeat_open"
