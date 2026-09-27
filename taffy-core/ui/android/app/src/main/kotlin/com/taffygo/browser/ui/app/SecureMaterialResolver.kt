// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Trusted-browser side of the opaque handle boundary; never bound into UI graphs. */
interface SecureMaterialResolver {
    /** Consumes a transient handle exactly once and transfers the recovered bytes. */
    suspend fun consume(handle: String): ByteArray
}
