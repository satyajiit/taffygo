// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * Screen SCR-416 — one vendor's sign-in, from the button to the credential.
 *
 * The whole page is one [stage] and four facts about the provider it is for.
 * There is no separate error field, no separate spinner flag and no separate
 * "connected" boolean beside the stage, because every one of those was a way
 * for two parts of the page to describe different moments at once.
 */
data class ProviderSignInUiState(
    /** Whether there is a vendor to draw, and what to say when there is not. */
    val status: Status = Status.LOADING,
    /** The catalog identity this page signs in to. */
    val providerId: String = "",
    /** The name to show, as the catalog spells it. */
    val displayName: String = "",
    /** Where the sign-in stands. */
    val stage: ProviderSignInStage = ProviderSignInStage.Idle,
    /**
     * Whether a credential is already filed for this provider. Separate from
     * [stage] on purpose: arriving at this page to sign in *again* is ordinary,
     * and a page that read a stored credential as a finished journey would give
     * a person no way to start one.
     */
    val connected: Boolean = false,
) {
    /** Whether a flow is underway, which is exactly when leaving it is offered. */
    val running: Boolean
        get() = when (stage) {
            ProviderSignInStage.Starting,
            is ProviderSignInStage.CodeReady,
            is ProviderSignInStage.Waiting,
            ProviderSignInStage.Exchanging,
            -> true

            else -> false
        }

    /** Whether pressing the page's one action would start a flow. */
    val startable: Boolean
        get() = status == Status.READY && !running

    /**
     * What the page has to say when it has no sign-in to draw.
     *
     * [NOT_BUILT], [NOT_CLEARED] and [UNKNOWN] are three different refusals and
     * read differently. A vendor this build compiles no flow for is a fact
     * about this version of TaffyGo; one whose flow is built and not permitted
     * to start is a fact about a review nobody has dated; a provider the roster
     * does not carry is a fact about the catalog. Collapsing any of them would
     * tell somebody to update the app over a provider that has simply left the
     * list, or over one whose sign-in is already sitting in the binary they
     * have.
     */
    enum class Status {
        /** The core has not published a roster yet. */
        LOADING,

        /** The roster arrived and does not carry this provider. */
        UNKNOWN,

        /**
         * The provider is here and either offers no plan sign-in, or offers one
         * this binary does not compile (decision 0081).
         */
        NOT_BUILT,

        /**
         * The flow is here and the browser will not start it, because no review
         * of what this vendor's terms permit has been dated (decision 0081).
         *
         * Not reachable from the hub, which does not offer such a row — but a
         * restored back stack or a link can arrive here, and the page then has
         * to say the true thing rather than the one that happens to be next to
         * it.
         */
        NOT_CLEARED,

        /** There is a sign-in to run. */
        READY,
    }
}
