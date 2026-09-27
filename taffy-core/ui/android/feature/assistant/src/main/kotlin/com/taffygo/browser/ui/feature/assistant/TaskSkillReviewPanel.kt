// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySavedFlowReview
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySavedFlowSceneImage
import com.taffygo.browser.ui.core.ui.taffyString

/** A compact offer opens the full ordered review before the explicit save action. */
@Composable
internal fun TaskSkillReviewPanel(
    state: TaskSkillReviewUiState,
    onReview: () -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    footerActions: Boolean = false,
) {
    val flow = state.review
    if (flow == null && state.skillId == null) return
    Column(
        modifier = modifier.fillMaxWidth().clip(TaffyTheme.shapes.card)
            .background(if (state.saved) TaffyTheme.colors.positiveWash else TaffyTheme.colors.ribbonThreeWash)
            .padding(TaffyTheme.spacing.snug).testTag(TASK_FLOW_OFFER_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = taffyString(if (state.saved) R.string.taffy_task_flow_saved else R.string.taffy_task_flow_offer),
                modifier = Modifier.weight(1f),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
            if (LocalDensity.current.fontScale < 1.5f) {
                TaffySavedFlowSceneImage(Modifier.width(96.dp))
            }
        }
        Text(
            text = taffyString(if (state.saved) R.string.taffy_task_flow_saved_body else R.string.taffy_task_flow_offer_body, (flow?.origin ?: state.origin)),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        if (state.failed && flow == null) Text(
            taffyString(R.string.taffy_task_flow_load_failed), color = TaffyTheme.colors.danger,
        )
        if (!state.saved && !footerActions) {
            TaffyPrimaryButton(
                label = taffyString(if (state.loading) R.string.taffy_task_flow_loading else R.string.taffy_task_flow_review),
                enabled = !state.loading,
                onClick = onReview,
                testTag = TASK_FLOW_REVIEW_TEST_TAG,
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_task_flow_not_now),
                onClick = onDismiss,
            )
        }
    }
}

@Composable
internal fun TaskSkillReviewDialog(
    state: TaskSkillReviewUiState,
    onAccept: () -> Unit,
    onClose: () -> Unit,
) {
    val review = state.review ?: return
    if (!state.showReview) return
    AlertDialog(
        onDismissRequest = onClose,
        modifier = Modifier.testTag(TASK_FLOW_DIALOG_TEST_TAG),
        containerColor = TaffyTheme.colors.surfaceRaised.copy(alpha = 1f),
        title = { Text(taffyString(R.string.taffy_task_flow_review_title)) },
        text = {
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                TaffySavedFlowReview(review)
                if (state.failed) Text(
                    text = taffyString(R.string.taffy_task_flow_failed),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.danger,
                )
            }
        },
        confirmButton = {
            TaffyPrimaryButton(
                label = taffyString(if (state.submitting) R.string.taffy_task_flow_saving else R.string.taffy_task_flow_save),
                onClick = onAccept,
                enabled = !state.submitting,
                testTag = TASK_FLOW_SAVE_TEST_TAG,
            )
        },
        dismissButton = {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_task_flow_back),
                onClick = onClose,
                enabled = !state.submitting,
            )
        },
    )
}

const val TASK_FLOW_OFFER_TEST_TAG: String = "task_flow_offer"
const val TASK_FLOW_REVIEW_TEST_TAG: String = "task_flow_review"
const val TASK_FLOW_SAVE_TEST_TAG: String = "task_flow_save"

const val TASK_FLOW_DIALOG_TEST_TAG: String = "task_flow_dialog"
