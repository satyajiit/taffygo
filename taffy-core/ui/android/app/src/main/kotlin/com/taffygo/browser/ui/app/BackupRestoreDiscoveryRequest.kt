// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.Closeable

/**
 * Withdraws one window's pending discovery without replaying work or changing a restore choice.
 * Closing is nonthrowing and revokes only undelivered presentation. A Ready review, once delivered,
 * belongs to its receiver and is withdrawn separately through the host.
 */
interface BackupRestoreDiscoveryRequest : Closeable {
    override fun close()
}
