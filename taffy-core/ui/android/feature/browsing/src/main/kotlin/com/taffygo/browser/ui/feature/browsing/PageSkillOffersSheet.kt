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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyBrandMark
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySavedFlowReview
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Accepted flows matched by the browser to the page currently in front of the person. */
@Composable
internal fun PageSkillOffersSheet(
    state: PageSkillOffersUiState,
    onClose: () -> Unit,
    onRefresh: () -> Unit,
    onStart: (PageSkillOffersUiState.Offer) -> Unit,
    onSetup: () -> Unit,
    onManage: () -> Unit = {},
    onLoadReviews: () -> Unit = {},
) {
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_page_flows_title),
        onDismissRequest = onClose,
        testTag = PAGE_FLOWS_TEST_TAG,
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth().clip(TaffyTheme.shapes.card)
                    .background(TaffyTheme.colors.ribbonThreeWash).padding(TaffyTheme.spacing.snug),
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                TaffyBrandMark(size = 44.dp, contentDescription = null)
                Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
                    if (state.host.isNotBlank()) Text(
                        text = state.host,
                        style = TaffyTheme.typography.title,
                        color = TaffyTheme.colors.textPrimary,
                    )
                    Text(
                        text = taffyString(availabilityText(state.availability)),
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textSecondary,
                    )
                }
            }
            state.failure?.let { failure ->
                Text(
                    text = taffyString(startFailureText(failure)),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.danger,
                )
            }
            state.offers.forEach { offer ->
                PageFlowCard(offer, state.starting, onStart, onSetup)
            }
            if (state.availability == PageSkillOffersUiState.Availability.REVIEW_UNAVAILABLE) {
                TaffySecondaryButton(
                    label = taffyString(if (state.reviewLoading) R.string.taffy_page_flows_review_loading else R.string.taffy_page_flows_review_load),
                    onClick = onLoadReviews,
                    enabled = !state.starting && !state.reviewLoading,
                    testTag = "page_saved_flows_load_reviews",
                )
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_page_flows_manage),
                    onClick = onManage,
                    enabled = !state.starting && !state.reviewLoading,
                    testTag = PAGE_FLOWS_MANAGE_TEST_TAG,
                )
            } else if (!state.starting && state.availability != PageSkillOffersUiState.Availability.LOADING) {
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_page_flows_refresh),
                    onClick = onRefresh,
                    testTag = PAGE_FLOWS_REFRESH_TEST_TAG,
                )
            }
        }
    }
}

@Composable
private fun PageFlowCard(
    offer: PageSkillOffersUiState.Offer,
    starting: Boolean,
    onStart: (PageSkillOffersUiState.Offer) -> Unit,
    onSetup: () -> Unit,
) {
    var expanded by remember(offer.id) { mutableStateOf(false) }
    Column(
        modifier = Modifier.fillMaxWidth().clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised).padding(TaffyTheme.spacing.snug),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Text(
            text = taffyPlural(R.plurals.taffy_page_flows_steps, offer.stepCount.toInt(), offer.stepCount.toInt()),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        offer.review?.let { review ->
            Text(
                text = review.startingAddress,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            TaffySecondaryButton(
                label = taffyString(if (expanded) R.string.taffy_page_flows_hide_steps else R.string.taffy_page_flows_view_steps),
                onClick = { expanded = !expanded },
            )
            if (expanded) TaffySavedFlowReview(review)
        }
        when (val start = offer.start) {
            is TaskStartDecision.Start -> {
                val consent = start.request.consent
                val learned = offer.review != null
                val route = routeText(consent.providerRoute)?.let { taffyString(it) }.orEmpty()
                Text(
                    text = if (learned) taffyString(R.string.taffy_page_flows_replay)
                    else taffyString(R.string.taffy_ask_disclosure_one_page, route),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
                Text(
                    text = if (consent.sourceDiscoveryEnabled) {
                        taffyPlural(
                            if (learned) R.plurals.taffy_page_flows_permission else R.plurals.taffy_task_start_discovery,
                            consent.newSourceCap, consent.newSourceCap,
                        )
                    } else taffyString(R.string.taffy_ask_scope_exact),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
                Text(
                    text = taffyString(if (learned) R.string.taffy_page_flows_handover else R.string.taffy_task_start_values),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
                TaffyPrimaryButton(
                    label = taffyString(if (starting) R.string.taffy_task_start_submitting else R.string.taffy_page_flows_use),
                    onClick = { onStart(offer) },
                    enabled = !starting,
                    testTag = PAGE_FLOW_START_TEST_TAG,
                )
            }
            is TaskStartDecision.Refused -> {
                Text(
                    text = taffyString(refusalText(start.reason)),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
                if (start.reason == TaskStartRefusal.SETUP_NEEDED) TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_page_flows_setup),
                    onClick = onSetup,
                )
            }
        }
    }
}

private fun availabilityText(value: PageSkillOffersUiState.Availability): Int = when (value) {
    PageSkillOffersUiState.Availability.LOADING -> R.string.taffy_page_flows_loading
    PageSkillOffersUiState.Availability.READY -> R.string.taffy_page_flows_ready
    PageSkillOffersUiState.Availability.EMPTY -> R.string.taffy_page_flows_empty
    PageSkillOffersUiState.Availability.STALE -> R.string.taffy_page_flows_stale
    PageSkillOffersUiState.Availability.REVIEW_UNAVAILABLE -> R.string.taffy_page_flows_review_unavailable
    PageSkillOffersUiState.Availability.UNAVAILABLE -> R.string.taffy_page_flows_unavailable
}

const val PAGE_FLOWS_TEST_TAG: String = "page_saved_flows"
const val PAGE_FLOWS_REFRESH_TEST_TAG: String = "page_saved_flows_refresh"
const val PAGE_FLOW_START_TEST_TAG: String = "page_saved_flow_start"
const val PAGE_FLOWS_MANAGE_TEST_TAG: String = "page_saved_flows_manage"
