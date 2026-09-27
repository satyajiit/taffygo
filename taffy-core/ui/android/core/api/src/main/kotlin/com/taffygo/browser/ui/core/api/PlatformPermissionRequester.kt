// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission

/**
 * The Window-owned platform permission boundary.
 *
 * Portable callers use the Core API's closed permission names and decisions. The Android host
 * keeps request codes, rationale rules, and system-settings intents behind this interface.
 */
interface PlatformPermissionRequester {

    /** The current decision and the safe next step for one permission. */
    fun current(permission: PlatformPermission): Snapshot

    /** Shows the system prompt only when [current] says another request is safe. */
    suspend fun request(permission: PlatformPermission): Snapshot

    /** Opens this app's system settings. Returns false when Android cannot open them. */
    fun openSettings(permission: PlatformPermission): Boolean

    /** A Core API decision plus Android's two facts that distinguish first and final denial. */
    data class Snapshot(
        val decision: PermissionDecision,
        val canRequest: Boolean,
        val shouldShowRationale: Boolean,
    ) {
        init {
            require(decision == PermissionDecision.DENIED || !canRequest)
            require(!shouldShowRationale || canRequest)
            require(!shouldShowRationale || decision == PermissionDecision.DENIED)
        }

        /** A denial Android will no longer present as a runtime prompt. */
        val requiresSystemSettings: Boolean
            get() = decision == PermissionDecision.DENIED && !canRequest
    }
}
