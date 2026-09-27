// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/**
 * What the store can offer a model request for one provider, right now.
 *
 * The freshness decision is made where the sealed record and the clock are
 * both in hand — inside the coordinator's critical section — so the browser
 * process receives an answer, never a record to interpret. Material crosses
 * the seam only when the answer is the material: a fresh access token, a raw
 * key, or the refresh token the one refresh leg needs.
 */
sealed interface ProviderAccess {

    /** A pasted key; the request header carries it as the vendor requires. */
    class RawKey(val material: ByteArray) : ProviderAccess

    /**
     * A subscription access token, fresh past the resolve margin.
     *
     * [credentialHost] is the origin this credential's requests belong to,
     * for the one vendor that issues an address with its token, and null for
     * everybody else. It travels with the material and only with it: an
     * address handed back beside a refusal would be a destination for a call
     * this answer said could not be made.
     */
    class AccessToken(
        val material: ByteArray,
        val expiresAtEpochMs: Long,
        val credentialHost: String?,
    ) : ProviderAccess

    /**
     * The access token is expired or inside the sixty-second resolve margin
     * (decision 0078); the browser must run the one refresh leg with this
     * rotation credential and store the result before proceeding.
     */
    class RefreshRequired(val refreshToken: ByteArray) : ProviderAccess

    /**
     * The record cannot be made usable without the person: the token is stale
     * and the vendor issued no refresh token, or the record failed to decode.
     */
    data object SignInRequired : ProviderAccess

    /** No record is stored for the provider at all. */
    data object NotConfigured : ProviderAccess
}
