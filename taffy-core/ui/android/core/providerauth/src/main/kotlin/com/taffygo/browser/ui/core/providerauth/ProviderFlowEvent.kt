// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/**
 * One browser-reported sign-in progress event.
 *
 * Carries no URI, no code material and no token — the verification URL and
 * user code of a device flow are the two values the vendor *asks* to be shown
 * to the person, and they are the only payload any event has.
 */
data class ProviderFlowEvent(
    /** The provider whose flow reports. */
    val providerId: String,
    /** Exact browser-minted flow identity this event belongs to. */
    val flowId: String,
    /** What happened. */
    val kind: ProviderFlowEventKind,
    /** Where to enter the code; only for [ProviderFlowEventKind.USER_CODE_READY]. */
    val verificationUrl: String? = null,
    /** The short code to enter; only for [ProviderFlowEventKind.USER_CODE_READY]. */
    val userCode: String? = null,
)
