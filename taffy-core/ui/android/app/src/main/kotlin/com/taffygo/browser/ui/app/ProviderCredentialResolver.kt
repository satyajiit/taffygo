// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Browser network-broker port. Compose receives only configured provider identifiers. */
interface ProviderCredentialResolver {
    /** Returns a fresh byte array owned by the caller, which must clear it after use. */
    suspend fun resolveProviderCredential(providerId: String): ByteArray
}
