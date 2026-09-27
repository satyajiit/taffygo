// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

/**
 * Screen SCR-404's one source of truth.
 *
 * It writes nothing. Saving a key, signing in to a plan and describing an
 * endpoint each happen on the surface that owns that credential, so this view
 * model reads two published flows and navigates — which is why it has no
 * failure path, no in-flight flag and no way to disagree with the core about
 * what is stored. The standing route is not among them: this screen lists what
 * is not set up, and nothing on it could carry a default.
 *
 * It remembers exactly one thing, and it is not about a provider: which tab the
 * person is reading.
 */
class ProviderHubViewModel @Inject constructor(
    private val roster: ProviderRosterRepository,
    private val credentials: ProviderCredentialsRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    /**
     * The category the person asked for, or null while they have asked for
     * none and the projection's own opening tab stands.
     *
     * Held as the intent that asked rather than as a bare group, because
     * `ProviderHubReducer` is then the only thing in the module that decides
     * what is on screen — there is no second expression of the same rule here
     * to drift away from it.
     */
    private val chosen = MutableStateFlow<ProviderHubIntent.ShowCategory?>(null)

    /**
     * What screen SCR-404 renders.
     *
     * The roster is live (decision 0080): a catalog refresh, a completed
     * sign-in or a key saved on the provider's own page all arrive here as the
     * next emission, so returning to the hub never needs a reload gesture and
     * never shows a stale list. Each of those emissions is a whole new
     * projection, so the person's tab is folded back on afterwards rather than
     * surviving inside it.
     */
    val state: StateFlow<ProviderHubUiState> = combine(
        roster.roster,
        credentials.configuredProviderIds,
        chosen,
    ) { rosterState, heldCredentialIds, category ->
        val projected = ProviderHubProjection.project(rosterState, heldCredentialIds)
        if (category == null) projected else ProviderHubReducer.reduce(projected, category)
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = ProviderHubUiState(),
        )

    /** Record the screen. The roster arrives by itself; nothing is loaded. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.AiAndProviders.screenId))
    }

    /**
     * Act on something the person did.
     *
     * The destination for a row comes from the row's own offer, so this cannot
     * send anybody somewhere their provider does not support — a blocked row
     * yields no destination and the press does nothing, which is also why the
     * screen draws it without a click action.
     */
    fun onIntent(intent: ProviderHubIntent, navigator: TaffyNavigator) {
        when (intent) {
            is ProviderHubIntent.ShowCategory -> chosen.value = intent

            is ProviderHubIntent.OpenRow ->
                ProviderRowDispatch.destinationFor(intent.row)?.let(navigator::goTo)

            ProviderHubIntent.AddYourOwnProvider ->
                navigator.goTo(TaffyDestination.CustomEndpointSetup())
        }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}
