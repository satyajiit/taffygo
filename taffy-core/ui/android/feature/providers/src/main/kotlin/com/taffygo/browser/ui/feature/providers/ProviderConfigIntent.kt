// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/** Everything screen SCR-415 can be asked to do. */
sealed interface ProviderConfigIntent {

    /** The draft changed. Carries what was typed and nothing about it. */
    data class ChangeKey(val draft: String) : ProviderConfigIntent

    /** Show or hide the characters in the field. */
    data object ToggleKeyVisible : ProviderConfigIntent

    /** Prove the draft with one bounded call, then store it (decision 0083). */
    data object SaveKey : ProviderConfigIntent

    /**
     * Store the draft although the provider was not definitively heard. Only
     * an indefinite verdict puts this on the table.
     */
    data object SaveKeyAnyway : ProviderConfigIntent

    /** Go to the vendor's sign-in (SCR-416). */
    data object StartSignIn : ProviderConfigIntent

    /** Go to this provider's models (SCR-417). */
    data object ChangeModel : ProviderConfigIntent

    /** Open the page where this vendor issues keys, in a tab. */
    data object OpenKeyPage : ProviderConfigIntent

    /** Open the vendor's documentation for this endpoint, in a tab. */
    data object OpenDocs : ProviderConfigIntent

    /** Send Taffy's model requests to a provider of the person's own. */
    data object UseForTaffy : ProviderConfigIntent

    /** Ask before removing the credential. */
    data object AskSignOut : ProviderConfigIntent

    /** Remove it. Reachable only from the confirmation. */
    data object ConfirmSignOut : ProviderConfigIntent

    /** Leave the credential alone. */
    data object CancelSignOut : ProviderConfigIntent
}
