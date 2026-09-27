// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.Closeable

/**
 * One window's opaque, single-use file-selection request. It carries no key and grants no deletion.
 * Closing it is nonthrowing and withdraws a pending selection without touching any document.
 */
interface BackupDeletionRequest : Closeable {
    override fun close()
}
