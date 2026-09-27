// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/** The narrow browser command port for one exact provider sign-in lifetime. */
interface ProviderSignInCommands {

    /** Admit one flow and return the browser-minted identity that owns it. */
    suspend fun start(providerId: String): String

    /** Cancel the exact still-live identity returned by [start]. */
    suspend fun cancel(flowId: String)

    /**
     * Hand what the vendor displayed, or the address the tab landed on, to the
     * exact still-live identity returned by [start] (decision 0095 section 2).
     *
     * Not a core command: the browser broker owns the redirect claim, and the
     * core learns of acceptance as an `EXCHANGING` event exactly as it does
     * for an intercepted redirect. `false` means no live flow accepted it.
     */
    suspend fun submitCode(flowId: String, entered: String): Boolean
}
