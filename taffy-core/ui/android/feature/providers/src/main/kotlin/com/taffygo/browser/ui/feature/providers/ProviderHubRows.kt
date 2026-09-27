// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyListRow
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One provider row: its mark, its name, what it takes, and where it stands.
 *
 * Nothing here asks who the provider is. The words come from the row's own
 * offer and availability, so a provider a catalog update introduced draws a
 * correct row on a binary compiled before it existed — the only lookup by
 * identity is the brand mark, which is a picture of who they are and never a
 * claim about what they offer.
 */
@Composable
internal fun ProviderHubRowItem(row: ProviderHubRow, onIntent: (ProviderHubIntent) -> Unit) {
    val detail = rowDetail(row)
    val state = taffyString(rowStateRes(row))
    val pressable = row.offer !is ProviderRowOffer.Blocked
    TaffyListRow(
        title = row.displayName,
        supporting = detail,
        accessibleDescription = taffyString(
            R.string.taffy_providers_row,
            row.displayName,
            detail,
            state,
        ),
        testTag = "$PROVIDER_ROW_TEST_TAG_PREFIX${row.providerId}",
        onClick = if (pressable) ({ onIntent(ProviderHubIntent.OpenRow(row)) }) else null,
        leading = { ProviderBadge(providerId = row.providerId, name = row.displayName) },
        trailing = { ProviderRowState(row = row, state = state) },
    )
}

/**
 * What this row would take, or why it cannot be taken.
 *
 * One line and no badge. A Default chip used to sit beside it, and it belonged
 * to a screen that also listed connected providers; here every row is a
 * provider that is not set up, so nothing on this screen could carry the
 * standing choice and a chip that never appeared would be a control nobody
 * could reach. SCR-419 says which provider a request goes to, over the rows
 * that can be it.
 */
@Composable
private fun ProviderRowState(row: ProviderHubRow, state: String) {
    // One colour. This line used to draw in `danger` for a single reason —
    // a served catalog had asked to move the provider's address and the core
    // had refused — and that reason left with the served catalog itself
    // (decision 0200). The remaining reasons are all ordinary facts about a
    // row this build cannot act on, and none of them is an alarm.
    Text(
        text = state,
        style = TaffyTheme.typography.label,
        color = TaffyTheme.colors.textSecondary,
    )
}

/**
 * The shape of the list before it has arrived.
 *
 * Three rows stand for "a list of these", not for a count nothing knows yet.
 * Only the first carries a description, so the reason the screen is empty is
 * heard once rather than three times.
 */
@Composable
internal fun ProviderHubSkeleton() {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .testTag(PROVIDER_HUB_LOADING_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        repeat(SkeletonRows) { index ->
            TaffySkeleton(
                modifier = Modifier.fillMaxWidth().height(SkeletonRowHeight),
                shape = TaffyTheme.shapes.row,
                accessibleDescription = if (index == 0) {
                    taffyString(R.string.taffy_providers_hub_loading)
                } else {
                    null
                },
            )
        }
    }
}

/**
 * The row's supporting line.
 *
 * What the provider would take, which on this screen is the whole of what
 * there is to say: no row here has a credential, so no row has an account name
 * or a plan to put in front of it.
 */
@Composable
private fun rowDetail(row: ProviderHubRow): String = taffyString(wayInRes(row.wayIn))

/**
 * One honest state per row, decided in the order it has to be read in.
 *
 * Three arms rather than the five this had. "Connected" and "not confirmed"
 * described a credential, and a row carrying one is filed under no group and
 * never reaches this screen — so those two words are said on SCR-419, where
 * they can be true.
 */
private fun rowStateRes(row: ProviderHubRow): Int {
    val offer = row.offer
    if (offer is ProviderRowOffer.Blocked) return blockedRes(offer.reason)
    if (row.signingIn) return R.string.taffy_providers_state_signing_in
    return invitationRes(offer)
}

private fun invitationRes(offer: ProviderRowOffer): Int = when (offer) {
    ProviderRowOffer.Configure -> R.string.taffy_providers_action_set_up
    ProviderRowOffer.SignIn -> R.string.taffy_providers_action_sign_in
    ProviderRowOffer.EditEndpoint -> R.string.taffy_providers_action_edit
    is ProviderRowOffer.Blocked -> blockedRes(offer.reason)
}

private fun blockedRes(reason: ProviderRowOffer.Reason): Int = when (reason) {
    ProviderRowOffer.Reason.NOT_ACTIONABLE -> R.string.taffy_providers_block_not_actionable
    ProviderRowOffer.Reason.HELD_SHUT -> R.string.taffy_providers_block_held_shut
    ProviderRowOffer.Reason.SIGN_IN_NOT_BUILT -> R.string.taffy_providers_block_sign_in_not_built
    ProviderRowOffer.Reason.SIGN_IN_NOT_CLEARED -> R.string.taffy_providers_block_sign_in_not_cleared
    ProviderRowOffer.Reason.NO_METHOD -> R.string.taffy_providers_block_no_method
}

private fun wayInRes(wayIn: ProviderWayIn): Int = when (wayIn) {
    ProviderWayIn.KEY -> R.string.taffy_providers_way_key
    ProviderWayIn.PLAN -> R.string.taffy_providers_way_plan
    ProviderWayIn.KEY_OR_PLAN -> R.string.taffy_providers_way_key_or_plan
    ProviderWayIn.OWN_ENDPOINT -> R.string.taffy_providers_way_endpoint
    ProviderWayIn.NONE -> R.string.taffy_providers_way_none
}

/** The tags screen SCR-404's semantics tests name. */
const val PROVIDER_ROW_TEST_TAG_PREFIX: String = "providers_row_"
const val PROVIDER_HUB_LOADING_TEST_TAG: String = "providers_hub_loading"

private val SkeletonRowHeight = 64.dp
private const val SkeletonRows = 3
