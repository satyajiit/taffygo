// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Profile-owned async endpoint for opaque, transient secure material. */
interface SecureStoreEndpoint {
    /** Returns fresh platform entropy without retaining it. */
    suspend fun generateEntropy(byteCount: Int): ByteArray

    /** Encrypts bounded material immediately and returns only an opaque handle. */
    suspend fun writeTransient(material: ByteArray): String

    /** Deletes an unspent transient handle. */
    suspend fun delete(handle: String): Boolean
}
