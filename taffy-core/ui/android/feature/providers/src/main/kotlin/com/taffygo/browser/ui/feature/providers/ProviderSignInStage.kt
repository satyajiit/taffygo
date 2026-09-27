// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.providerauth.ProviderSignInFailure

/**
 * Where one vendor sign-in stands, as screen SCR-416 draws it.
 *
 * One member per state the flow is really in, because the surface this replaces
 * had one: a row that said "Enter code" and then went dead. Every member below
 * names something a person can do next, and the two that cannot — [Exchanging]
 * and [Succeeded] — say so rather than looking idle.
 *
 * "Connected" is deliberately derived rather than announced. The browser clears
 * its flow the moment it seals the credential, so what makes a sign-in a
 * success is the roster filing one; [Succeeded] is that fact observed after a
 * flow this screen watched, never this union asserting it.
 */
sealed interface ProviderSignInStage {

    /** Nothing is running. The page offers to start. */
    data object Idle : ProviderSignInStage

    /** The admission is on its way to the core; the vendor has not been asked. */
    data object Starting : ProviderSignInStage

    /**
     * The device flow minted a code and it is the person's to enter.
     *
     * [remainingSeconds] counts down **this screen's own waiting window**, not
     * the vendor's expiry and not the browser's deadline — neither of which
     * reaches Android. It runs out into [Cancelled] through the exact flow's
     * portable cancellation command.
     */
    data class CodeReady(
        /** Where to enter the code, exactly as the vendor gave it. */
        val verificationUrl: String,
        /** The short code the vendor asks to be shown. */
        val userCode: String,
        /** Seconds left on this screen's waiting window, never below zero. */
        val remainingSeconds: Int,
    ) : ProviderSignInStage

    /**
     * The authorization is in the person's hands somewhere else — a tab is
     * open on the vendor's page — and there is no code to show.
     *
     * [codeEntry] is the way back when the tab does not return (decision 0095
     * section 2): a flow that finishes through a redirect carries a field to
     * paste what the vendor showed into, and a flow that finishes by polling
     * carries null and is a plain wait. The browser stays the authority on
     * which is which; this only decides what to draw.
     */
    data class Waiting(
        /** The manual-code field, or null for a wait that has no way back. */
        val codeEntry: ManualCodeEntry?,
    ) : ProviderSignInStage

    /** The grant came back and the browser is exchanging and sealing it. */
    data object Exchanging : ProviderSignInStage

    /** The roster now holds a credential that this screen watched arrive. */
    data object Succeeded : ProviderSignInStage

    /** The attempt ended without a credential, and the page says why. */
    data class Failed(val failure: ProviderSignInFailure) : ProviderSignInStage

    /** The exact flow was removed and its native work was asked to stop. */
    data object Cancelled : ProviderSignInStage
}
