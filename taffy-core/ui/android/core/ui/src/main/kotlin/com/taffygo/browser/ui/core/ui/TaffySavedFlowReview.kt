// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.SavedFlowReview

/** The same complete, value-free review in a finished task and in saved-skill settings. */
@Composable
fun TaffySavedFlowReview(review: SavedFlowReview, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.fillMaxWidth().testTag(SAVED_FLOW_REVIEW_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().clip(TaffyTheme.shapes.card)
                .background(TaffyTheme.colors.ribbonThreeWash)
                .padding(TaffyTheme.spacing.snug),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Text(
                text = taffyString(R.string.taffy_flow_review_origin, review.origin),
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = taffyString(R.string.taffy_flow_review_start),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            Text(
                text = review.startingAddress,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.testTag(SAVED_FLOW_ADDRESS_TEST_TAG),
            )
        }
        Text(
            text = taffyString(R.string.taffy_flow_review_steps),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        review.steps.forEachIndexed { index, step -> FlowReviewStep(index, step) }
        Text(
            text = taffyString(R.string.taffy_flow_review_privacy),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

@Composable
private fun FlowReviewStep(index: Int, step: SavedFlowReview.Step) {
    val handover = step.action == SavedFlowReview.Action.HANDOVER || step.personPurpose != null
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Box(
            modifier = Modifier.size(32.dp).clip(CircleShape).background(
                if (handover) TaffyTheme.colors.accentWash else TaffyTheme.colors.surfaceSunken,
            ),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                text = (index + 1).toString(),
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary,
            )
        }
        Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
            val target = step.target?.let { taffyString(flowTargetLabel(it)) }
            Text(
                text = if (target != null) {
                    taffyString(R.string.taffy_flow_review_named_action, taffyString(flowActionLabel(step.action)), target)
                } else {
                    taffyString(flowActionLabel(step.action))
                },
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
            if (index > 0) step.address?.let {
                Text(it, style = TaffyTheme.typography.detail, color = TaffyTheme.colors.textSecondary)
            }
            step.personPurpose?.let {
                Text(
                    text = taffyString(flowPersonLabel(it)),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
}

private fun flowActionLabel(action: SavedFlowReview.Action): Int = when (action) {
    SavedFlowReview.Action.OPEN_PAGE -> R.string.taffy_flow_open
    SavedFlowReview.Action.READ_PAGE -> R.string.taffy_flow_read
    SavedFlowReview.Action.FIND_CONTROL -> R.string.taffy_flow_find
    SavedFlowReview.Action.CHOOSE_CONTROL -> R.string.taffy_flow_choose
    SavedFlowReview.Action.FOCUS_CONTROL -> R.string.taffy_flow_focus
    SavedFlowReview.Action.OPEN_LINK -> R.string.taffy_flow_link
    SavedFlowReview.Action.SCROLL -> R.string.taffy_flow_scroll
    SavedFlowReview.Action.HANDOVER -> R.string.taffy_flow_handover
    SavedFlowReview.Action.INSPECT_FORM -> R.string.taffy_flow_inspect_form
    SavedFlowReview.Action.FILL_FORM -> R.string.taffy_flow_fill_form
    SavedFlowReview.Action.SUBMIT_FORM -> R.string.taffy_flow_submit_form
    SavedFlowReview.Action.DOWNLOAD -> R.string.taffy_flow_download
    SavedFlowReview.Action.READ_DOWNLOADS -> R.string.taffy_flow_downloads
    SavedFlowReview.Action.INSPECT_PDF -> R.string.taffy_flow_pdf
    SavedFlowReview.Action.BACK -> R.string.taffy_flow_back
    SavedFlowReview.Action.FORWARD -> R.string.taffy_flow_forward
    SavedFlowReview.Action.RELOAD -> R.string.taffy_flow_reload
    SavedFlowReview.Action.STOP_LOADING -> R.string.taffy_flow_stop_loading
    SavedFlowReview.Action.OPEN_TAB -> R.string.taffy_flow_open_tab
}

private fun flowTargetLabel(target: SavedFlowReview.Target): Int = when (target) {
    SavedFlowReview.Target.SIGN_IN -> R.string.taffy_flow_target_sign_in
    SavedFlowReview.Target.SIGN_OUT -> R.string.taffy_flow_target_sign_out
    SavedFlowReview.Target.SEARCH -> R.string.taffy_flow_target_search
    SavedFlowReview.Target.ADD_TO_CART -> R.string.taffy_flow_target_add_to_cart
    SavedFlowReview.Target.VIEW_CART -> R.string.taffy_flow_target_view_cart
    SavedFlowReview.Target.CHECKOUT -> R.string.taffy_flow_target_checkout
    SavedFlowReview.Target.CONTINUE -> R.string.taffy_flow_target_continue
    SavedFlowReview.Target.CONFIRM -> R.string.taffy_flow_target_confirm
    SavedFlowReview.Target.CANCEL -> R.string.taffy_flow_target_cancel
    SavedFlowReview.Target.NEXT_PAGE -> R.string.taffy_flow_target_next_page
    SavedFlowReview.Target.DOWNLOAD -> R.string.taffy_flow_target_download
}

private fun flowPersonLabel(purpose: SavedFlowReview.PersonPurpose): Int = when (purpose) {
    SavedFlowReview.PersonPurpose.IDENTITY_NUMBER -> R.string.taffy_flow_person_identity
    SavedFlowReview.PersonPurpose.VERIFICATION -> R.string.taffy_flow_person_verification
    SavedFlowReview.PersonPurpose.ONE_TIME_CODE -> R.string.taffy_flow_person_code
    SavedFlowReview.PersonPurpose.FORM_DETAILS -> R.string.taffy_flow_person_details
}

const val SAVED_FLOW_REVIEW_TEST_TAG: String = "saved_flow_review"
const val SAVED_FLOW_ADDRESS_TEST_TAG: String = "saved_flow_address"
