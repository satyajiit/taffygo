// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.ErrandPagePort
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.credentials.ProviderKeyProber
import com.taffygo.browser.ui.core.credentials.ProviderProbeVerdict
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
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
 * Screen SCR-415's one source of truth.
 *
 * It holds one thing of its own — the draft — and reads everything else. The
 * three consequential things a person can do here (prove and store a key,
 * remove a credential, send Taffy's requests this way) each go through the seam
 * that owns them and come back as a published fact; none of them is reported
 * from local state, so this screen cannot say connected about something the
 * core has not filed.
 */
class ProviderConfigViewModel @Inject constructor(
    private val roster: ProviderRosterRepository,
    private val credentials: ProviderCredentialsRepository,
    private val prober: ProviderKeyProber,
    private val preferences: UserPreferencesRepository,
    private val errand: ErrandPagePort,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    /** Which provider this page is about, from the route that opened it. */
    private val providerId: String =
        savedState.get<String>(TaffyDestination.PROVIDER_ID).orEmpty()

    private val draft = MutableStateFlow(ProviderConfigDraft())

    /** What screen SCR-415 renders. */
    val state: StateFlow<ProviderConfigUiState> = combine(
        roster.roster,
        roster.models,
        credentials.configuredProviderIds,
        draft,
    ) { rosterState, models, heldCredentialIds, local ->
        ProviderConfigProjection.project(
            providerId = providerId,
            roster = rosterState,
            models = models,
            browserHeldCredentialIds = heldCredentialIds,
            draft = local,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = ProviderConfigUiState(providerId = providerId),
        )

    /** Record the screen. The roster arrives by itself; nothing is loaded. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.ProviderConfig(providerId).screenId),
        )
    }

    /** Act on something the person did. */
    fun onIntent(intent: ProviderConfigIntent, navigator: TaffyNavigator) {
        when (intent) {
            ProviderConfigIntent.SaveKey -> proveThenStore()
            ProviderConfigIntent.SaveKeyAnyway -> storeUnproved()
            ProviderConfigIntent.ConfirmSignOut -> forgetCredential()
            ProviderConfigIntent.UseForTaffy -> useForTaffy()
            ProviderConfigIntent.StartSignIn ->
                navigator.goTo(TaffyDestination.ProviderSignIn(providerId))

            ProviderConfigIntent.ChangeModel ->
                navigator.goTo(TaffyDestination.ModelSelection(providerId))

            ProviderConfigIntent.OpenKeyPage ->
                openPage(state.value.presentation?.getKeyUrl, navigator)

            ProviderConfigIntent.OpenDocs ->
                openPage(state.value.presentation?.docsUrl, navigator)

            else -> draft.update { ProviderConfigReducer.reduce(it, intent) }
        }
    }

    /**
     * Prove the draft with one bounded model call, then store it (decision
     * 0083).
     *
     * A usable verdict saves. A definitive refusal keeps the draft and names
     * its category, because the next thing to do is correct it. An indefinite
     * verdict — the provider was not definitively heard — keeps the draft and
     * offers to save anyway, since an unreachable endpoint is not a wrong key.
     */
    private fun proveThenStore() {
        val form = state.value.keyForm ?: return
        if (form.busy) return
        if (form.verdict != ProviderKeyForm.Verdict.PLAUSIBLE) {
            draft.update { it.copy(problem = localProblem(form.verdict)) }
            return
        }
        val typed = draft.value.key
        draft.update { it.copy(probing = true, problem = null, stored = false) }
        viewModelScope.launch {
            val material = typed.toByteArray()
            try {
                val verdict = try {
                    prober.probeApiKey(providerId, material)
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (_: Exception) {
                    // The test could not run — a probe already in flight, or a
                    // core that is not ready. Nothing was judged, so the answer
                    // is indefinite and the draft stays savable.
                    null
                }
                if (verdict == ProviderProbeVerdict.USABLE) {
                    store(material)
                } else {
                    draft.update { it.copy(probing = false, problem = problemOf(verdict)) }
                }
            } finally {
                material.fill(0)
            }
        }
    }

    /**
     * Store the draft although the provider was not definitively heard.
     *
     * Reachable only from an indefinite verdict's offer, and the guard is
     * repeated here rather than trusted from the screen: a definitive refusal
     * must not become a stored key by way of a stale button.
     */
    private fun storeUnproved() {
        val form = state.value.keyForm ?: return
        if (form.busy || !form.offersSaveAnyway) return
        if (form.verdict != ProviderKeyForm.Verdict.PLAUSIBLE) return
        val typed = draft.value.key
        draft.update { it.copy(probing = true, problem = null) }
        viewModelScope.launch {
            val material = typed.toByteArray()
            try {
                store(material)
            } finally {
                material.fill(0)
            }
        }
    }

    /**
     * Hand the bytes to the store, and let the roster say what happened.
     *
     * The route moves with a successful store, and only with one: saving a key
     * *is* the choice to use your own provider, so asking for it a second time
     * would be asking a person to confirm what they just did. A key that did
     * not save is not a route.
     */
    private suspend fun store(material: ByteArray) {
        try {
            credentials.saveApiKey(providerId, material)
            preferences.setProviderRoute(ProviderRoute.DIRECT_WITH_YOUR_KEY)
            draft.update {
                it.copy(key = "", revealed = false, probing = false, problem = null, stored = true)
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            // Never surface a keystore diagnostic: it is not user-facing copy
            // and it can name what it was asked to hold.
            draft.update {
                it.copy(probing = false, problem = ProviderKeyProblem.STORE_FAILED)
            }
        }
    }

    /**
     * Remove the credential after the explicit second step.
     *
     * The browser attempts the vendor-side revocation best-effort on the way
     * (decision 0081); what this screen owns is the forget. A forget that fails
     * leaves the page saying connected, which is the truthful rendering of a
     * credential that is in fact still stored.
     */
    private fun forgetCredential() {
        if (!draft.value.confirmingSignOut || draft.value.signingOut) return
        draft.update { it.copy(confirmingSignOut = false, signingOut = true) }
        viewModelScope.launch {
            try {
                credentials.forget(providerId)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                // The row stays connected because the record is still there;
                // no separate error channel says less than the page itself.
            } finally {
                draft.update { it.copy(signingOut = false) }
            }
        }
    }

    /** Send Taffy's model requests to a provider of the person's own. */
    private fun useForTaffy() {
        if (state.value.defaultChoice != ProviderDefaultChoice.OFFERED) return
        viewModelScope.launch {
            preferences.setProviderRoute(ProviderRoute.DIRECT_WITH_YOUR_KEY)
        }
    }

    /**
     * Open one of the catalog's links on its own surface.
     *
     * "Fetching a key is an errand a person comes back from" is what this
     * comment said while the code opened a tab and sent the person to the
     * browsing surface, where the way back to their half-typed key was the tab
     * switcher. Screen SCR-110 is that sentence made true: back returns here,
     * with the draft intact.
     *
     * [navigator] is unused because opening an errand is what shows it, decided
     * in the browser so a page cannot be opened where nobody is looking. It
     * stays in the signature because every intent here is dispatched the same
     * way.
     */
    private fun openPage(url: String?, @Suppress("UNUSED_PARAMETER") navigator: TaffyNavigator) {
        if (url.isNullOrBlank()) return
        viewModelScope.launch { errand.open(url) }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L

        /** What the form itself refused, said in the same vocabulary. */
        fun localProblem(verdict: ProviderKeyForm.Verdict): ProviderKeyProblem = when (verdict) {
            ProviderKeyForm.Verdict.EMPTY -> ProviderKeyProblem.EMPTY
            ProviderKeyForm.Verdict.PREFIX_MISMATCH -> ProviderKeyProblem.PREFIX_MISMATCH
            ProviderKeyForm.Verdict.PLAUSIBLE -> ProviderKeyProblem.UNSETTLED
        }

        /** The page's rendering of what one bounded probe call answered. */
        fun problemOf(verdict: ProviderProbeVerdict?): ProviderKeyProblem = when (verdict) {
            ProviderProbeVerdict.AUTH -> ProviderKeyProblem.KEY_REFUSED
            ProviderProbeVerdict.BILLING -> ProviderKeyProblem.BILLING_REFUSED
            ProviderProbeVerdict.MODEL_NOT_FOUND -> ProviderKeyProblem.MODEL_NOT_FOUND
            ProviderProbeVerdict.RATE_LIMIT -> ProviderKeyProblem.RATE_LIMITED
            ProviderProbeVerdict.OVERLOADED -> ProviderKeyProblem.OVERLOADED
            ProviderProbeVerdict.TIMEOUT -> ProviderKeyProblem.TIMED_OUT
            ProviderProbeVerdict.NETWORK -> ProviderKeyProblem.UNREACHABLE
            ProviderProbeVerdict.UNKNOWN -> ProviderKeyProblem.UNSETTLED
            ProviderProbeVerdict.NO_MODEL_LISTED -> ProviderKeyProblem.NO_MODEL_LISTED
            ProviderProbeVerdict.USABLE -> ProviderKeyProblem.UNSETTLED
            null -> ProviderKeyProblem.TEST_UNAVAILABLE
        }
    }
}
