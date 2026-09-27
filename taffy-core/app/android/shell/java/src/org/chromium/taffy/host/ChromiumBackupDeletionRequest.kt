// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupDeletionRequest

/** The window register, not caller-supplied identity, owns a single pending metadata lookup. */
internal class ChromiumBackupDeletionRequest(
    private val window: ChromiumBackupWindowState,
) : BackupDeletionRequest {
    private var claimed = false

    fun claimLocked(): Boolean {
        if (window.closed || this !in window.deletionRequests || claimed) return false
        claimed = true
        return true
    }

    override fun close() {
        synchronized(window.lock) { window.deletionRequests.remove(this) }
    }
}
