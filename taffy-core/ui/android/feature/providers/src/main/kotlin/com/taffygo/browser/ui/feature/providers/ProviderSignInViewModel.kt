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
import com.taffygo.browser.ui.core.providerauth.ProviderSignInEngine
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFlows
import com.taffygo.browser.ui.core.providerauth.ProviderSignInState
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * Screen SCR-416's one source of truth.
 *
 * The flow itself belongs to the browser: it opens the vendor's surface, polls,
 * exchanges and seals, and the profile's `ProviderSignInEngine` is the one
 * place its progress is folded into something a screen can draw. So this view
 * model starts or cancels a flow, keeps the code's own clock, holds the
 * manual-code draft, and reads the roster to learn whether a credential
 * actually arrived.
 */
class ProviderSignInViewModel @Inject constructor(
    private val roster: ProviderRosterRepository,
    private val signIn: ProviderSignInEngine,
    private val errand: ErrandPagePort,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
    /**
     * The compiled flow map, as a parameter so a host test can state the rule
     * it is exercising rather than depend on which vendors happen to be
     * cleared today. Production passes the default; the shipping map's own
     * contents are checked against the browser's table by the `catalog` lane.
     */
    private val signInFlows: Map<String, Boolean> = ProviderSignInFlows.byVendor,
    /** The redirect-shaped half of that map, for the same reason. */
    private val pkceVendors: Set<String> = ProviderSignInFlows.pkceVendors,
) : ViewModel() {

    /** Which vendor this page signs in to, from the route that opened it. */
    private val providerId: String =
        savedState.get<String>(TaffyDestination.PROVIDER_ID).orEmpty()

    private val local = MutableStateFlow(Local())

    /** What screen SCR-416 renders. */
    val state: StateFlow<ProviderSignInUiState> = combine(
        roster.roster,
        signIn.states,
        local,
    ) { rosterState, signIns, here ->
        ProviderSignInProjection.project(
            providerId = providerId,
            roster = rosterState,
            engine = signIns[providerId],
            flowRan = here.flowRan,
            cancelled = here.cancelled,
            codeShownForSeconds = here.codeShownForSeconds,
            manualCode = here.manualCode,
            signInFlows = signInFlows,
            pkceVendors = pkceVendors,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = ProviderSignInUiState(providerId = providerId),
        )

    init {
        viewModelScope.launch {
            signIn.states
                .map { it[providerId] }
                .distinctUntilChanged()
                .collectLatest {
                    endErrandWhenTheVendorIsDone(it)
                    runCodeClock(it)
                }
        }
    }

    /** Whether this flow was last seen waiting on the vendor's own page. */
    private var awaitingVendor = false

    /**
     * Takes the vendor's page down the moment this flow stops waiting on it.
     *
     * The redirect that ends a PKCE sign-in is claimed by a navigation
     * throttle answering `CANCEL_AND_IGNORE` — deliberately, so a sign-in that
     * worked does not finish on an error page. The consequence is that the
     * vendor's own page stays exactly where it was and nothing about it will
     * ever change again. While that page was an ordinary tab nobody noticed:
     * this screen was one back press underneath it. On a route of its own it
     * is the whole window, and it hides the very screen that says the sign-in
     * worked. It was reported from a phone as a sign-in that stuck at the last
     * step; it had in fact succeeded, behind the frozen page.
     *
     * Waiting on the vendor is the only state this surface belongs to, so it
     * is closed on leaving that state rather than on any one terminal:
     * exchanging the code, a refusal, a cancellation and the deadline all end
     * a person's business with the vendor's page equally.
     *
     * Two things keep this from reaching an errand that is not this flow's.
     * It acts only on the transition out of waiting, so an errand opened while
     * nothing was running is untouched; and it closes by the id that is
     * actually up rather than by assumption, which the browser then matches
     * against the errand it holds before closing anything.
     */
    private fun endErrandWhenTheVendorIsDone(engine: ProviderSignInState?) {
        if (engine is ProviderSignInState.AwaitingAuthorization) {
            awaitingVendor = true
            return
        }
        if (!awaitingVendor) return
        awaitingVendor = false
        errand.page.value?.errandId?.let(errand::close)
    }

    /** Record the screen. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.ProviderSignIn(providerId).screenId),
        )
    }

    /** Act on something the person did. */
    fun onIntent(intent: ProviderSignInIntent, navigator: TaffyNavigator) {
        when (intent) {
            ProviderSignInIntent.Start -> start()
            ProviderSignInIntent.Cancel -> cancel()

            ProviderSignInIntent.DismissFailure -> {
                signIn.dismissFailure(providerId)
                local.update { Local() }
            }

            ProviderSignInIntent.OpenVerificationPage -> openVerificationPage(navigator)

            is ProviderSignInIntent.ManualCodeChanged -> changeManualCode(intent.entered)
            ProviderSignInIntent.SubmitManualCode -> submitManualCode()

            ProviderSignInIntent.OpenProviderPage ->
                navigator.goTo(TaffyDestination.ProviderConfig(providerId))

            // The code reaches the clipboard from the composable that has a
            // context; nothing about a user code is this view model's to hold.
            ProviderSignInIntent.CopyUserCode -> Unit
        }
    }

    private fun start() {
        if (state.value.status != ProviderSignInUiState.Status.READY) return
        if (state.value.running) return
        local.update { Local(flowRan = true) }
        viewModelScope.launch { signIn.connect(providerId) }
    }

    /** Mark cancellation only after portable admission accepts exact removal. */
    private fun cancel() {
        if (!state.value.running) return
        viewModelScope.launch {
            if (signIn.cancel(providerId)) {
                local.update { it.copy(cancelled = true, codeShownForSeconds = 0) }
            }
        }
    }

    /**
     * Opens the address the vendor answered with, on its own surface.
     *
     * It used to open a tab and send the person to the browsing surface, which
     * lost them: the code and its countdown were still running back here, and
     * the way back to them was the tab switcher. An errand page keeps the way
     * back to this screen as the back button, with the countdown still going.
     *
     * [navigator] is unused because opening an errand is what shows it — one
     * act, decided in the browser, so a page can never be opened somewhere
     * nobody is looking. It stays in the signature because every intent on this
     * screen is dispatched the same way.
     */
    private fun openVerificationPage(@Suppress("UNUSED_PARAMETER") navigator: TaffyNavigator) {
        val stage = state.value.stage as? ProviderSignInStage.CodeReady ?: return
        viewModelScope.launch { errand.open(stage.verificationUrl) }
    }

    /**
     * Keep the manual-code draft, whole.
     *
     * An entry past [MAX_MANUAL_ENTRY_BYTES] is left as it was rather than
     * cut: the browser would refuse it at the same bound, and a value cut on
     * this side would be handed over as something the vendor never showed.
     * A change clears the last refusal, because the refusal was about the
     * value that is no longer there.
     */
    private fun changeManualCode(entered: String) {
        if (entered.toByteArray(Charsets.UTF_8).size > MAX_MANUAL_ENTRY_BYTES) return
        local.update {
            it.copy(manualCode = ManualCodeEntry(draft = entered, rejected = false, submitting = false))
        }
    }

    /**
     * Hand the draft to the browser for the exact running flow (decision
     * 0095 section 2).
     *
     * Acceptance is not a state of this screen: the browser reports the
     * exchange through the engine, exactly as it does for an intercepted
     * redirect, and the draft is cleared so the field does not still show a
     * code the flow has already spent. Refusal keeps the draft and says so,
     * because the likeliest cause is a partial paste and the fix is to look
     * at what is in the field.
     */
    private fun submitManualCode() {
        // The stage says whether this flow offers a code entry at all, and that
        // is the engine's fact. The draft is read from the local slice instead,
        // because that is where a keystroke lands at once: the projected state
        // is a combine and trails it by an emission, so a submit in the same
        // frame as the last keystroke would read an empty draft and hand the
        // browser nothing.
        if ((state.value.stage as? ProviderSignInStage.Waiting)?.codeEntry == null) return
        val entry = local.value.manualCode
        if (!entry.submittable) return
        local.update { it.copy(manualCode = entry.copy(rejected = false, submitting = true)) }
        viewModelScope.launch {
            val accepted = signIn.submitCode(providerId, entry.draft)
            local.update {
                it.copy(
                    manualCode = if (accepted) {
                        ManualCodeEntry()
                    } else {
                        it.manualCode.copy(rejected = true, submitting = false)
                    },
                )
            }
        }
    }

    /**
     * Count this screen's waiting window down while a code is up.
     *
     * Restarted from zero whenever the engine's state for this provider changes
     * — a new code is a new window — and stopped by the same change, because
     * `collectLatest` cancels the previous run. Reaching the end leaves the
     * flow through the same exact cancellation as the button.
     */
    private suspend fun runCodeClock(engine: ProviderSignInState?) {
        val waiting = engine as? ProviderSignInState.AwaitingAuthorization
        if (waiting?.userCode == null) {
            local.update { it.copy(codeShownForSeconds = 0) }
            return
        }
        local.update { it.copy(codeShownForSeconds = 0) }
        var elapsed = 0
        while (elapsed < ProviderSignInProjection.WAITING_WINDOW_SECONDS) {
            delay(TICK_MILLIS)
            elapsed++
            local.update { it.copy(codeShownForSeconds = elapsed) }
        }
        cancel()
    }

    /**
     * What is true only on this screen: whether it started or cancelled a
     * flow, how long a code has been up, and the manual-code draft.
     */
    private data class Local(
        val flowRan: Boolean = false,
        val cancelled: Boolean = false,
        val codeShownForSeconds: Int = 0,
        val manualCode: ManualCodeEntry = ManualCodeEntry(),
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val TICK_MILLIS = 1_000L

        /**
         * The most a manual entry may weigh, in UTF-8 bytes.
         *
         * The browser is the authority — `kMaxManualEntryBytes` in
         * `taffy-core/browser/providerauth/profile_provider_auth_broker.cc`
         * refuses anything longer — and this is the same number restated so
         * the field stops growing where the browser would stop reading,
         * rather than accepting a paste it is about to be told no about.
         */
        const val MAX_MANUAL_ENTRY_BYTES = 8 * 1024
    }
}
