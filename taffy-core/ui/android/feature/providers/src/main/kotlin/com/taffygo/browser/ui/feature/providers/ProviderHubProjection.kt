// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFlows

/**
 * Screen SCR-404's pure half: the roster, read as three categories of rows.
 *
 * Hand it a roster and the browser's handles, and every word, count and
 * destination the screen can draw is decided, including which tab it opens on.
 * The one thing it does not decide is which tab a person afterwards asks for;
 * that is `ProviderHubReducer`, folded on top of whatever this last produced,
 * so a republication cannot move somebody off the tab they are reading.
 *
 * The standing route is not read here and no row carries a Default badge. Where
 * a request would go is a fact about a provider that is set up, so it is stated
 * on SCR-419 and on a provider's own page — the two screens whose rows can hold
 * it — through the one rule `ProviderRowDispatch.standingChoice`.
 */
object ProviderHubProjection {

    /**
     * Fold the three published facts into the screen state.
     *
     * A roster that is not ready yields no rows at all, whatever it carries:
     * `ProviderRosterState.ready` is the only thing that separates a core that
     * has not spoken from a catalog with nothing in it, and rendering a
     * pre-publication list as the catalog would make "still coming" invisible.
     */
    fun project(
        roster: ProviderRosterState,
        browserHeldCredentialIds: Set<String>,
        signInFlows: Map<String, Boolean> = ProviderSignInFlows.byVendor,
    ): ProviderHubUiState {
        if (!roster.ready) return ProviderHubUiState(status = ProviderHubStatus.LOADING)
        val rows = roster.rows.flatMap { row ->
            toHubRows(row, browserHeldCredentialIds, signInFlows)
        }
        val sections = sectionsOf(rows)
        return ProviderHubUiState(
            status = statusOf(rows = rows, rosterCarriedRows = roster.rows.isNotEmpty()),
            sections = sections,
            showing = openingCategory(sections),
        )
    }

    /**
     * The tab the screen opens on before anybody has chosen one.
     *
     * The first category that holds something, so the screen opens on a tab
     * with providers on it rather than on an explanation of why one is empty.
     * With nothing at all the screen is drawing its empty state and no tab is
     * on show; the first group is then the honest place to be standing.
     */
    private fun openingCategory(sections: List<ProviderHubSection>): ProviderHubGroup =
        sections.firstOrNull()?.group ?: ProviderHubGroup.entries.first()

    /**
     * One roster row becomes one hub row *per group it is filed under*, which
     * for a provider that is already connected is none at all.
     *
     * A provider offering both a plan and a key is in two groups, and each
     * entry answers its own group's question: under Subscription the row
     * offers the vendor's sign-in, under Bring your own key it offers the key
     * form. One category is drawn at a time, so the two entries never share a
     * list and `providerId` stays a unique key within each one.
     *
     * A connected provider yields no rows because it is not something to add;
     * `ProviderRowDispatch.groupsFor` states that, and screen SCR-419 is where
     * it is then read.
     */
    private fun toHubRows(
        row: ProviderRosterRow,
        browserHeldCredentialIds: Set<String>,
        signInFlows: Map<String, Boolean>,
    ): List<ProviderHubRow> {
        val availability = ProviderRowDispatch.availabilityFor(row, browserHeldCredentialIds)
        return ProviderRowDispatch.groupsFor(row, availability).map { group ->
            ProviderHubRow(
                providerId = row.providerId,
                displayName = row.displayName,
                group = group,
                offer = ProviderRowDispatch.offerFor(row, signInFlows, group),
                wayIn = ProviderRowDispatch.wayInFor(row),
                signingIn = row.signingIn,
            )
        }
    }

    /** Groups in declaration order, and only the ones that hold something. */
    private fun sectionsOf(rows: List<ProviderHubRow>): List<ProviderHubSection> =
        ProviderHubGroup.entries.mapNotNull { group ->
            rows.filter { it.group == group }
                .takeIf { it.isNotEmpty() }
                ?.let { ProviderHubSection(group = group, rows = it) }
        }

    /**
     * Why there is nothing to add, told apart from there being nothing at all.
     *
     * [rosterCarriedRows] is the roster *before* the connected providers are
     * dropped, and it is the only thing that separates a catalog this browser
     * could not read from one whose every provider is already set up. Both
     * draw a blank page and only one of them is a fact about the catalog.
     */
    private fun statusOf(
        rows: List<ProviderHubRow>,
        rosterCarriedRows: Boolean,
    ): ProviderHubStatus = when {
        rows.isEmpty() && rosterCarriedRows -> ProviderHubStatus.ALL_CONNECTED
        rows.isEmpty() -> ProviderHubStatus.EMPTY
        rows.all { it.offer is ProviderRowOffer.Blocked } -> ProviderHubStatus.BLOCKED
        else -> ProviderHubStatus.READY
    }
}
