// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/** Everything screen SCR-416 can be asked to do. */
sealed interface ProviderSignInIntent {

    /** Begin the vendor's sign-in. */
    data object Start : ProviderSignInIntent

    /** Cancel the exact running flow through portable admission. */
    data object Cancel : ProviderSignInIntent

    /** Open the vendor's verification page in a tab. */
    data object OpenVerificationPage : ProviderSignInIntent

    /** Put the user code on the clipboard. */
    data object CopyUserCode : ProviderSignInIntent

    /**
     * The manual-code field changed (decision 0095 section 2). The value is
     * what the vendor displayed or the address its page landed on, whole.
     */
    data class ManualCodeChanged(val entered: String) : ProviderSignInIntent

    /** Hand the manual-code draft to the browser for the running flow. */
    data object SubmitManualCode : ProviderSignInIntent

    /** Dismiss a shown failure and return to rest. */
    data object DismissFailure : ProviderSignInIntent

    /** Go on to what the credential is for: this provider's page. */
    data object OpenProviderPage : ProviderSignInIntent
}
