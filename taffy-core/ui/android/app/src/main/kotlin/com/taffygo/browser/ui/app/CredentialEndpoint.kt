// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Window-owned endpoint for Android's visible credential surface. */
interface CredentialEndpoint {
    /** Requests one Google credential bound to browser-supplied protocol state. */
    suspend fun requestGoogleCredential(
        flowId: String,
        serverClientId: String,
        hashedNonce: String,
    )
}

/** Google receives only the public, lowercase SHA-256 digest of the raw nonce. */
internal fun String.isValidGoogleNonceHash(): Boolean =
    length == GOOGLE_NONCE_HASH_HEX_BYTES && all { it in '0'..'9' || it in 'a'..'f' }

private const val GOOGLE_NONCE_HASH_HEX_BYTES = 64
