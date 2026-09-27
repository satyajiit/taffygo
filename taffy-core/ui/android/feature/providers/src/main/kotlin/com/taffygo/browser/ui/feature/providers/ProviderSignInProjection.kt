// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFlows
import com.taffygo.browser.ui.core.providerauth.ProviderSignInState

/**
 * Screen SCR-416's pure half: the engine's state, the roster and one clock,
 * read as one stage.
 *
 * Every stage the page can be in is reachable from this function with plain
 * values, which is the point: the surface being replaced could only be seen by
 * signing in to a vendor for real, so nobody ever saw what it did after the
 * code appeared.
 */
object ProviderSignInProjection {

    /**
     * How long this screen waits on a device code before it stops.
     *
     * The vendor's own expiry and the browser's flow deadline are the real
     * clocks and neither reaches Android — the flow event carries a URL and a
     * code and nothing else. So this is the surface's own window, counted down
     * where a person can see it and named as such in the copy, and running out
     * of it requests the same exact cancellation as the button.
     */
    const val WAITING_WINDOW_SECONDS: Int = 600

    /**
     * Fold what the browser reported, what the roster holds and how long the
     * code has been up into the page.
     *
     * @param flowRan whether a sign-in was started from this screen. It is what
     *   separates "you signed in just now" from "you were already signed in",
     *   which look identical in the roster and must not read the same.
     * @param cancelled whether exact cancellation was accepted.
     * @param codeShownForSeconds how long the current code has been on screen.
     * @param manualCode this screen's own manual-code draft (decision 0095
     *   section 2), drawn only on a redirect wait for a vendor in
     *   [pkceVendors]; a device-code vendor's wait never shows it.
     */
    fun project(
        providerId: String,
        roster: ProviderRosterState,
        engine: ProviderSignInState?,
        flowRan: Boolean,
        cancelled: Boolean,
        codeShownForSeconds: Int,
        manualCode: ManualCodeEntry = ManualCodeEntry(),
        signInFlows: Map<String, Boolean> = ProviderSignInFlows.byVendor,
        pkceVendors: Set<String> = ProviderSignInFlows.pkceVendors,
    ): ProviderSignInUiState {
        if (!roster.ready) {
            return ProviderSignInUiState(
                status = ProviderSignInUiState.Status.LOADING,
                providerId = providerId,
            )
        }
        val row = roster.rows.firstOrNull { it.providerId == providerId }
            ?: return ProviderSignInUiState(
                status = ProviderSignInUiState.Status.UNKNOWN,
                providerId = providerId,
            )
        val connected = row.stored != null
        // Everything the *row* has to say for a plan sign-in to make sense.
        val rowOffersAPlan = RosterAuthMethod.OAUTH in row.authMethods &&
            row.enabled &&
            row.configurable
        // And separately, what this binary may do about it. Three values, not
        // two: absent is no flow at all, false is a flow the browser refuses
        // to start because no review of the vendor's terms is dated, and only
        // true is a sign-in that can run.
        val carried = signInFlows[row.providerId]
        if (!rowOffersAPlan || carried != true) {
            return ProviderSignInUiState(
                // Built-and-not-cleared is said only when that is the one
                // thing missing. Where the row itself offers no plan, whether
                // a flow happens to be compiled for the vendor is beside the
                // point and would be the wrong sentence to lead with.
                status = if (rowOffersAPlan && carried == false) {
                    ProviderSignInUiState.Status.NOT_CLEARED
                } else {
                    ProviderSignInUiState.Status.NOT_BUILT
                },
                providerId = row.providerId,
                displayName = row.displayName,
                connected = connected,
            )
        }
        return ProviderSignInUiState(
            status = ProviderSignInUiState.Status.READY,
            providerId = row.providerId,
            displayName = row.displayName,
            stage = stageOf(
                engine = engine,
                connected = connected,
                flowRan = flowRan,
                cancelled = cancelled,
                codeShownForSeconds = codeShownForSeconds,
                // The field is offered where a redirect can fail to return,
                // which is a fact about the flow's shape rather than about
                // this attempt: the browser reports the same bare wait for
                // both shapes, so the shape is read from the compiled map.
                codeEntry = manualCode.takeIf { row.providerId in pkceVendors },
            ),
            connected = connected,
        )
    }

    /**
     * The stage, in precedence order, and the order is the argument.
     *
     * A live engine fact comes first. An event held during a refused cancel is
     * released by the engine and therefore still outranks the local request.
     * Accepted cancellation comes next, even when an older credential remains
     * connected. Only then does a newly filed credential mean this flow
     * succeeded: the browser clears its own state the instant it seals one, so
     * the roster is what distinguishes completion from never having started.
     */
    private fun stageOf(
        engine: ProviderSignInState?,
        connected: Boolean,
        flowRan: Boolean,
        cancelled: Boolean,
        codeShownForSeconds: Int,
        codeEntry: ManualCodeEntry?,
    ): ProviderSignInStage = when {
        engine != null -> fromEngine(engine, codeShownForSeconds, codeEntry)
        cancelled -> ProviderSignInStage.Cancelled
        connected && flowRan -> ProviderSignInStage.Succeeded
        else -> ProviderSignInStage.Idle
    }

    private fun fromEngine(
        engine: ProviderSignInState,
        codeShownForSeconds: Int,
        codeEntry: ManualCodeEntry?,
    ): ProviderSignInStage = when (engine) {
        ProviderSignInState.Idle -> ProviderSignInStage.Idle
        ProviderSignInState.Starting -> ProviderSignInStage.Starting
        ProviderSignInState.Exchanging -> ProviderSignInStage.Exchanging
        is ProviderSignInState.Failed -> ProviderSignInStage.Failed(engine.failure)
        is ProviderSignInState.AwaitingAuthorization -> {
            val url = engine.verificationUrl
            val code = engine.userCode
            // Both or neither: a device flow states an address *and* a code,
            // and half of that pair is a panel a person cannot act on. The
            // PKCE flows carry neither and are waiting — with the way back
            // that decision 0095 section 2 requires drawn beside the wait.
            if (url != null && code != null) {
                ProviderSignInStage.CodeReady(
                    verificationUrl = url,
                    userCode = code,
                    remainingSeconds = remaining(codeShownForSeconds),
                )
            } else {
                ProviderSignInStage.Waiting(codeEntry = codeEntry)
            }
        }
    }

    private fun remaining(codeShownForSeconds: Int): Int =
        (WAITING_WINDOW_SECONDS - codeShownForSeconds).coerceAtLeast(0)
}
