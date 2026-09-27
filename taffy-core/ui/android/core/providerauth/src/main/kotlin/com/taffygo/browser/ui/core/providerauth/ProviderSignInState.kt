// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/**
 * Where one provider's sign-in stands, as a screen renders it.
 *
 * "Connected" is deliberately absent: whether a credential is in force is the
 * roster's fact (stored, subscription-backed, usable), confirmed by the core,
 * never asserted by this state machine. What lives here is only the journey
 * between pressing Connect and the roster answering.
 */
sealed interface ProviderSignInState {

    /** No sign-in is running. */
    data object Idle : ProviderSignInState

    /** The admission is on its way to the core. */
    data object Starting : ProviderSignInState

    /**
     * The vendor's authorization is in the person's hands: a Custom Tab is
     * open, or a device code is waiting to be entered.
     */
    data class AwaitingAuthorization(
        /** Where to enter the code, for the device flow; null for PKCE. */
        val verificationUrl: String?,
        /** The short code to enter, for the device flow; null for PKCE. */
        val userCode: String?,
    ) : ProviderSignInState

    /** The grant returned and the browser is exchanging and sealing it. */
    data object Exchanging : ProviderSignInState

    /** The attempt ended without a credential, and the screen says why. */
    data class Failed(val failure: ProviderSignInFailure) : ProviderSignInState
}
