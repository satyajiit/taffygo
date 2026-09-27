// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.annotation.StringRes
import com.taffygo.browser.ui.core.task.TaffyReadiness

/**
 * The set-up panel's title for a verdict that needs set-up, on the Ask
 * overlay: a route chosen with nothing behind it is told what is missing
 * rather than that nothing was ever set up.
 */
@StringRes
internal fun askSetupTitle(readiness: TaffyReadiness): Int = when (readiness) {
    is TaffyReadiness.RouteChosenButNothingBehindIt -> R.string.taffy_ask_setup_route_title
    TaffyReadiness.Unknown,
    TaffyReadiness.NotSetUp,
    is TaffyReadiness.Ready,
    -> R.string.taffy_ask_setup_title
}

/**
 * The set-up panel's body: what is missing when a route was chosen, otherwise
 * the three ways in. Only one route a request can take names anything to be
 * missing, so every route reads as that one rather than as a second string
 * nobody would see.
 */
@StringRes
internal fun askSetupBody(readiness: TaffyReadiness): Int = when (readiness) {
    is TaffyReadiness.RouteChosenButNothingBehindIt ->
        R.string.taffy_ask_setup_route_direct_body
    TaffyReadiness.Unknown,
    TaffyReadiness.NotSetUp,
    is TaffyReadiness.Ready,
    -> R.string.taffy_ask_setup_body
}

/**
 * Why an ask in place has no page on it, or `null` when it has one or there is
 * nothing true to say (decision 0168).
 *
 * The overlay had one sentence here and it was about private tabs, so a person
 * who opened it on one of Taffy's own tabs was told "This kind of task reads
 * pages you name. Tap to choose them." and nothing else — an instruction whose
 * only answer is a sheet that has already decided this tab is not on offer.
 * The sheet has said the true thing all along; this is the same sentence where
 * the question is actually asked, which is why it is that string and not a
 * third wording of it.
 *
 * Taffy's own tab is tested before the private one because a tab can be both,
 * and `privateTabsWithheld` excludes Taffy's on purpose — a tab Taffy opened
 * was never the person's to offer, so privacy is not what withheld it.
 */
@StringRes
internal fun noPagesLine(state: AddressBarUiState): Int? = when {
    !state.conditions.asksInPlace -> null
    state.attachedPages.isNotEmpty() -> null
    state.pages.currentTabIsTaffys -> R.string.taffy_attach_pages_taffy_tabs
    state.pages.privateTabsWithheld -> R.string.taffy_ask_will_do_none_private
    else -> null
}
