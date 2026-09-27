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
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * Screen SCR-419's one source of truth.
 *
 * It holds two identifiers of its own — whose confirmation is open and whose
 * removal is running — and reads everything else. The removal itself goes
 * through `ProviderCredentialsRepository`, which is the same seam screen
 * SCR-415 removes a credential through, and behind that seam sits the one
 * writer decision
 * `docs/decisions/0078-a-provider-credential-has-one-writer.md` requires. So
 * two screens offering the same act is not two writers: it is one writer with
 * two doors, and the revoke-first ordering and the per-provider critical
 * section apply to both of them identically.
 *
 * What the two screens *do* share directly is the warning, in
 * `ProviderRemovalSheet`. That is the part a second copy would let drift.
 */
class ConnectedProvidersViewModel @Inject constructor(
    private val roster: ProviderRosterRepository,
    private val credentials: ProviderCredentialsRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val confirming = MutableStateFlow<String?>(null)
    private val signingOut = MutableStateFlow<String?>(null)

    /** What screen SCR-419 renders. */
    val state: StateFlow<ConnectedProvidersUiState> = combine(
        roster.roster,
        roster.models,
        credentials.configuredProviderIds,
        combine(confirming, signingOut) { open, running -> open to running },
    ) { rosterState, models, heldCredentialIds, local ->
        ConnectedProvidersProjection.project(
            roster = rosterState,
            models = models,
            browserHeldCredentialIds = heldCredentialIds,
            confirmingProviderId = local.first,
            signingOutProviderId = local.second,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = ConnectedProvidersUiState(),
        )

    /** Record the screen. The roster arrives by itself; nothing is loaded. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.ConnectedProviders.screenId))
    }

    /** Act on something the person did. */
    fun onIntent(intent: ConnectedProvidersIntent, navigator: TaffyNavigator) {
        when (intent) {
            is ConnectedProvidersIntent.OpenRow ->
                ProviderRowDispatch.destinationFor(intent.row.providerId, intent.row.offer)
                    ?.let(navigator::goTo)

            // Never while a removal is running: a confirmation that reopens
            // over a request already sent invites a second one.
            is ConnectedProvidersIntent.AskSignOut ->
                if (signingOut.value == null && intent.row.canSignOut) {
                    confirming.value = intent.row.providerId
                }

            ConnectedProvidersIntent.ConfirmSignOut -> forgetCredential()
            ConnectedProvidersIntent.CancelSignOut -> confirming.value = null

            // Pushed rather than replacing this screen: a person who went
            // looking for something to add and changed their mind expects one
            // press of back to put them where they were.
            ConnectedProvidersIntent.AddProvider ->
                navigator.goTo(TaffyDestination.AiAndProviders)
        }
    }

    /**
     * Remove the credential after the explicit second step.
     *
     * The browser attempts the vendor-side revocation best-effort on the way
     * (decision 0081); what this screen owns is the forget. A forget that
     * fails leaves the row on the list, which is the truthful rendering of a
     * credential that is in fact still stored.
     */
    private fun forgetCredential() {
        val providerId = confirming.value ?: return
        if (signingOut.value != null) return
        confirming.value = null
        signingOut.value = providerId
        viewModelScope.launch {
            try {
                credentials.forget(providerId)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                // The row stays because the record is still there; no separate
                // error channel says less than the list itself.
            } finally {
                signingOut.update { if (it == providerId) null else it }
            }
        }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}
