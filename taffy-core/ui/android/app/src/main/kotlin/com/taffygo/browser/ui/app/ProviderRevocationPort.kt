// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/**
 * Hands a signed-out subscription's token to the browser process for
 * vendor-side revocation, per decision
 * `docs/decisions/0081-provider-subscription-sign-in-is-browser-run-and-vendor-gated.md`.
 *
 * Best-effort by contract: the sealed record is deleted whatever the vendor
 * answers, because the person's device is theirs to clear. So the port
 * returns nothing, must not throw for a vendor that cannot be reached, and
 * must not retain the token after returning — the caller zeroes its copy the
 * moment this returns. The browser's network half owns the actual request;
 * this seam exists because only Android can open the sealed record, and only
 * the browser may talk to a vendor.
 */
fun interface ProviderRevocationPort {
    fun revokeBestEffort(providerId: String, token: ByteArray)
}
