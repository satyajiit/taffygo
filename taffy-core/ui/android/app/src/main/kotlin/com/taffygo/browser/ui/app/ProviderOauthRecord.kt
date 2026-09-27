// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/**
 * One provider's subscription tokens, sealed as a single store record.
 *
 * The triple is one value on purpose (decision
 * `docs/decisions/0078-a-provider-credential-has-one-writer.md`): a refresh
 * replaces access token, refresh token and expiry together in one
 * [AndroidProfileSecureMaterialStore.storeProviderCredential] write, so no
 * crash or race can leave a new access token beside an already-spent refresh
 * token. Rotation is atomic by construction rather than by discipline.
 *
 * Token bytes are [ByteArray] so the holder can zero them; this class never
 * copies them and [zero] is the owner's obligation once the value has been
 * encoded or used.
 */
class ProviderOauthRecord(
    /** The token scheme the vendor issued, almost always `Bearer`. */
    val tokenType: String,
    /** When the access token stops working, as epoch milliseconds. */
    val expiresAtEpochMs: Long,
    /** The granted scopes, space-joined; empty when the vendor named none. */
    val scopes: String,
    /** The value a request's `Authorization` header carries. */
    val accessToken: ByteArray,
    /** The rotation credential, absent when the vendor issued none. */
    val refreshToken: ByteArray?,
    /**
     * The origin this particular credential's requests go to, absent for
     * every vendor whose address is the one the catalog names.
     *
     * It is part of the value rather than a fact beside it: one vendor issues
     * the address together with the token, a renewal may answer with a
     * different one, and an address that outlived the token it belongs to
     * would send the next request to the previous account's server. Not a
     * secret, and not zeroed — it is a host name, and the browser validates it
     * again before it addresses anything with it.
     */
    val credentialHost: String? = null,
) {
    /** Wipes both token buffers. The record is unusable afterwards. */
    fun zero() {
        accessToken.fill(0)
        refreshToken?.fill(0)
    }
}
