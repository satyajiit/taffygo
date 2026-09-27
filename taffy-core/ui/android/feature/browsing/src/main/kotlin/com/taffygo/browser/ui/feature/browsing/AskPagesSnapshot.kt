// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab

/**
 * Open tabs this Ask surface may offer, after private, Taffy-owned, and
 * nowhere tabs have been left out.
 */
data class AskPagesSnapshot(
    val status: Status = Status.READY,
    val eligibleTabs: List<Tab> = emptyList(),
    val privateTabsWithheld: Boolean = false,
    val taffyTabsPresent: Boolean = false,
    /**
     * Whether the tab the person is looking at is one of Taffy's own.
     *
     * Separate from [taffyTabsPresent], which is about the list: a person can
     * have a Taffy tab open somewhere and be reading their own page, and only
     * one of those two facts explains why an ask in place has no page on it.
     */
    val currentTabIsTaffys: Boolean = false,
) {
    /** Whether the list is loading, ready, or cannot be read. */
    enum class Status {
        LOADING,
        READY,
        UNAVAILABLE,
    }
}

/** Project open tabs into the Ask eligibility table. */
internal fun askPagesSnapshot(
    tabs: List<Tab>,
    status: AskPagesSnapshot.Status = AskPagesSnapshot.Status.READY,
): AskPagesSnapshot = AskPagesSnapshot(
    status = status,
    eligibleTabs = tabs.filter(::isEligibleAskTab),
    privateTabsWithheld = TaskScope.privateTabsWithheld(tabs),
    taffyTabsPresent = tabs.any { it.isTaffyTab },
    currentTabIsTaffys = currentAskTab(tabs)?.isTaffyTab == true,
)

/** A user tab with a host. Never private, never Taffy's, never nowhere. */
internal fun isEligibleAskTab(tab: Tab): Boolean =
    !tab.isPrivate && !tab.isTaffyTab && !tab.hasBeenNowhere && tab.host.isNotBlank()
