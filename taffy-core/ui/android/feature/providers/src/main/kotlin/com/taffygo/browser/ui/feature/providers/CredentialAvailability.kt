// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * Whether a provider has a credential behind it — three-valued, on purpose.
 *
 * Two halves answer this and they can disagree: the core's roster says what it
 * has recorded, and the browser's secure store says which handles it holds. A
 * two-valued reading has to pick a winner, and picking the roster means a
 * momentary gap between a save and its echo re-locks a provider that is
 * working perfectly well.
 *
 * [UNKNOWN] is what that gap deserves. It is also what a stored credential the
 * vendor has not confirmed deserves — a refresh that failed is a transient
 * failure, not the removal of a credential — so neither case can take a
 * working row back to [ABSENT].
 */
enum class CredentialAvailability {
    /** A credential is stored and the core expects it to work. */
    PRESENT,

    /**
     * Something is stored and whether it works is not settled: the vendor
     * wants the person again, a refresh failed, or the browser holds a handle
     * the core has not echoed yet.
     */
    UNKNOWN,

    /** Neither half holds anything for this provider. */
    ABSENT,
}
