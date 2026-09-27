// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * Screen SCR-419 — every provider this browser can currently reach a model
 * through.
 *
 * The hub lists what could be set up; this lists what is. The difference
 * matters to somebody with five providers in the catalog and one key: the hub
 * is a shop and this is a shelf, and a person coming to change or remove
 * something should not have to read past four things they never connected.
 *
 * The only thing this screen owns is which row's confirmation is open. What is
 * connected, what it is set to, and where a request would go are all read from
 * the published roster, so the list cannot claim a credential the core has not
 * filed.
 */
data class ConnectedProvidersUiState(
    /** Whether there is a list, and what to say when there is not. */
    val status: Status = Status.LOADING,
    /** The connected providers, in the order the core published them. */
    val rows: List<ConnectedProviderRow> = emptyList(),
    /** The row whose sign-out confirmation is open, or null. */
    val confirming: ConnectedProviderRow? = null,
) {
    /**
     * What the page has to say when it has no rows.
     *
     * [EMPTY] is not an error and not a missing list: it is the truthful state
     * of a phone where nothing has been connected yet, which is where everyone
     * starts.
     */
    enum class Status {
        /** The core has not published a roster yet. */
        LOADING,

        /** The roster arrived and nothing on it has a credential or an address. */
        EMPTY,

        /** There is a list to draw. */
        READY,
    }

    /**
     * Whether this screen should hand the person over to SCR-404.
     *
     * Stated here rather than as a condition inside the composable so that the
     * rule is one a host test can call. It is [Status.EMPTY] and nothing else:
     * a roster that has not arrived is [Status.LOADING], so nobody is ever
     * forwarded on the strength of a list the core has not published, and a
     * screen with rows on it is where they asked to be.
     */
    val handsOverToTheHub: Boolean get() = status == Status.EMPTY
}
