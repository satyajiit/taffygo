// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * Screen SCR-404 — every provider this browser could reach a model through
 * that is not already set up, and what each one would take.
 *
 * The screen holds nothing of its own except which category is on screen: there
 * is no draft, no open sheet and no local optimism about what a person just
 * did, because every control here navigates and the surface it navigates to
 * owns the change. So this is the roster read one way, and the next roster the
 * core publishes replaces it whole — carrying the person's chosen tab forward,
 * which is the one thing about this screen a republication must not reset.
 */
data class ProviderHubUiState(
    /** Whether there is a list, and what to say when there is not. */
    val status: ProviderHubStatus = ProviderHubStatus.LOADING,
    /** The non-empty groups, in group order. */
    val sections: List<ProviderHubSection> = emptyList(),
    /**
     * The category whose rows are on screen.
     *
     * Always one of the three, and always drawn: a tab that holds nothing says
     * so in words rather than disappearing, because a row of tabs that changes
     * length as the roster arrives is a row a person cannot learn.
     */
    val showing: ProviderHubGroup = ProviderHubGroup.SUBSCRIPTION,
) {
    /** Every row across every group, for a reader that wants the flat list. */
    val rows: List<ProviderHubRow> by lazy(LazyThreadSafetyMode.NONE) {
        buildList {
            for (section in sections) addAll(section.rows)
        }
    }

    private val sectionsByGroup: Map<ProviderHubGroup, ProviderHubSection> by
        lazy(LazyThreadSafetyMode.NONE) { sections.associateBy(ProviderHubSection::group) }

    /** The rows in one category, empty when it holds none. */
    fun rowsIn(group: ProviderHubGroup): List<ProviderHubRow> =
        sectionsByGroup[group]?.rows.orEmpty()

    /**
     * How many providers one category holds.
     *
     * The section's own count, which is its row list measured, so a tab reading
     * three over a list of two is not a state this screen can reach. A category
     * with no section at all holds nothing, and says zero rather than nothing.
     */
    fun countIn(group: ProviderHubGroup): Int =
        sectionsByGroup[group]?.count ?: 0
}
