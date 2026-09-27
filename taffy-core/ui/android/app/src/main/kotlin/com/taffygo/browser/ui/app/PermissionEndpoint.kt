// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import taffy.core_api.PlatformPermission

/** Window-owned endpoint for closed Android platform permission requests. */
interface PermissionEndpoint {
    /** Starts one request; its terminal result returns through the Core API. */
    fun request(requestId: String, permission: PlatformPermission)
}
