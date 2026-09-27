// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * The one trusted platform action that may ask Android to make TaffyGo the default browser.
 *
 * A feature receives only this narrow operation. It cannot construct a role intent, claim the
 * role changed, or retry a dismissal. The production implementation additionally requires the
 * current Activity to be visible and records a successful sheet launch once for the application.
 */
interface BrowserRoleOffer {
    /** Offer Android's own role sheet after one explicit, sourced-output acceptance gesture. */
    fun offerAfterAcceptedOutput(): BrowserRoleOfferResult
}
