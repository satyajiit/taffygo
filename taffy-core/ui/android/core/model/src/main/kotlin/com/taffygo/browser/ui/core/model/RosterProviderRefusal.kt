// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The last thing a provider refused a request for, in the vendor's own terms.
 *
 * Deliberately not a member of [RosterCredentialState]. That enum says what the
 * *credential* is — present and expected to work, needing a sign-in, or last
 * refreshed unsuccessfully — and none of its members can say "this key works
 * and the vendor is refusing to spend it", which is the only thing a person
 * whose month has run out needs to be told.
 */
enum class RosterProviderRefusal {
    /** Too many requests too quickly. It clears by itself. */
    RATE_LIMIT,

    /** The account will not spend: no balance, no plan, or a payment failure. */
    BILLING,

    /** The vendor has capacity trouble of its own and is shedding requests. */
    OVERLOADED,
}
