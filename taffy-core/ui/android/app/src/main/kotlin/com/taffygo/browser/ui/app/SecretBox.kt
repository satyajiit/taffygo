// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.Closeable

/** Authenticated-encryption primitive hidden behind the profile material store. */
internal interface SecretBox : Closeable {
    suspend fun seal(material: ByteArray): String
    suspend fun open(sealed: String): ByteArray

    /** Permanently removes the key when the profile itself is being erased. */
    suspend fun destroy() = close()
}
