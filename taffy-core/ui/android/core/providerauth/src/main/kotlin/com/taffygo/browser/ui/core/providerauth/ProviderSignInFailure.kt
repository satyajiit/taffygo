// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/** Why one sign-in attempt ended without a credential. */
enum class ProviderSignInFailure {
    /** The person denied or dismissed the vendor's authorization page. */
    DENIED,

    /** The vendor answered with a protocol failure. */
    PROVIDER_ERROR,

    /** The flow's deadline passed before the person finished. */
    TIMED_OUT,

    /** The flow could not run — the surface, the network or the core. */
    UNAVAILABLE,

    /** The core refused the admission — the row does not offer this today. */
    NOT_ADMITTED,
}
