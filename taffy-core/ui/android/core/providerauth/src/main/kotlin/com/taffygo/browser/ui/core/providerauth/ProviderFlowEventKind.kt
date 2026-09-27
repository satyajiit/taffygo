// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/** One progress fact the browser reports about a running sign-in. */
enum class ProviderFlowEventKind {
    /** The authorization surface is open and the person is deciding. */
    AWAITING_AUTHORIZATION,

    /** The device flow minted a user code; the event carries it. */
    USER_CODE_READY,

    /** The grant arrived and the token exchange is running. */
    EXCHANGING,

    /** The credential is sealed and announced; the roster now says so. */
    COMPLETED,

    /** The person denied or dismissed the authorization. */
    FAILED_DENIED,

    /** The vendor answered with a protocol failure. */
    FAILED_PROVIDER,

    /** The flow's deadline passed. */
    FAILED_DEADLINE,

    /** The flow could not run or was cut off. */
    FAILED_UNAVAILABLE,
}
