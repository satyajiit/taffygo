// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFlows

/**
 * Screen SCR-419's pure half: the roster read as the shorter list.
 *
 * Every rule it needs already exists on `ProviderRowDispatch` and is asked
 * rather than restated — which is the whole reason this screen is small. What
 * counts as connected, which provider carries the standing choice, and where a
 * row leads are all one function each, shared with the hub and with a
 * provider's own page, so three screens a person reads one after another
 * cannot disagree about them.
 */
object ConnectedProvidersProjection {

    /**
     * Fold the published facts into the list.
     *
     * A roster that is not ready yields no rows whatever it carries, for the
     * reason the hub gives: `ready` is the only thing separating a core that
     * has not spoken from a catalog with nothing in it, and an empty
     * pre-publication list drawn as the answer would tell somebody nothing is
     * connected a moment before their providers arrive.
     */
    fun project(
        roster: ProviderRosterState,
        models: Map<String, List<ProviderModel>>,
        browserHeldCredentialIds: Set<String>,
        confirmingProviderId: String? = null,
        signingOutProviderId: String? = null,
        signInFlows: Map<String, Boolean> = ProviderSignInFlows.byVendor,
    ): ConnectedProvidersUiState {
        if (!roster.ready) return ConnectedProvidersUiState()
        val availability = roster.rows.associate {
            it.providerId to ProviderRowDispatch.availabilityFor(it, browserHeldCredentialIds)
        }
        val standing = ProviderRowDispatch.standingChoice(availability, roster.rows)
        val rows = roster.rows
            .filter { ProviderRowDispatch.configuredFor(it, availability.getValue(it.providerId)) }
            .map { row ->
                toConnectedRow(
                    row = row,
                    availability = availability.getValue(row.providerId),
                    models = models,
                    standing = standing,
                    signingOut = row.providerId == signingOutProviderId,
                    signInFlows = signInFlows,
                )
            }
        return ConnectedProvidersUiState(
            status = if (rows.isEmpty()) {
                ConnectedProvidersUiState.Status.EMPTY
            } else {
                ConnectedProvidersUiState.Status.READY
            },
            rows = rows,
            // Read back out of the list rather than held beside it, so the
            // sheet is always about a row that is still on the page: a
            // credential that disappears from the roster while its
            // confirmation is open closes the confirmation with it.
            confirming = rows.firstOrNull { it.providerId == confirmingProviderId && it.canSignOut },
        )
    }

    private fun toConnectedRow(
        row: ProviderRosterRow,
        availability: CredentialAvailability,
        models: Map<String, List<ProviderModel>>,
        standing: String?,
        signingOut: Boolean,
        signInFlows: Map<String, Boolean>,
    ): ConnectedProviderRow = ConnectedProviderRow(
        providerId = row.providerId,
        displayName = row.displayName,
        availability = availability,
        accountLabel = row.stored?.accountLabel,
        planLabel = row.stored?.planLabel,
        // A pin the roster no longer carries shows as no pin, which is what it
        // now is; the provider's own order is what a request then follows.
        modelName = row.selectedModelId?.let { pinned ->
            models[row.providerId]?.firstOrNull { it.modelId == pinned }?.displayName
        },
        thinking = row.thinking,
        ownEndpoint = row.origin == RosterProviderOrigin.CUSTOM ||
            row.catalogLayer == RosterCatalogLayer.USER_OVERRIDE,
        carriesStandingChoice = row.providerId == standing,
        // The roster's own record is what can be removed. A row that is here
        // only because the browser holds a handle the core has not echoed yet
        // offers nothing to sign out of, because there is nothing on the
        // roster to remove and saying otherwise would offer an act that does
        // nothing.
        canSignOut = row.stored != null,
        signingOut = signingOut,
        offer = ProviderRowDispatch.offerFor(row, signInFlows),
        // Carried whole. Which sentence it becomes is the composable's
        // business; that it is said at all is this list's (registry document
        // section 5.4: a refusal reaches a surface, not a log).
        lastRefusal = row.lastRefusal,
    )
}
