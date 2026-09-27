// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.Closeable

/**
 * One window's exact exported or freshly selected document, never a saved URI or access to others.
 * The caller must obtain a separate, resumed confirmation before deletion. One invocation spends
 * this handle even if its outcome is unknown. Closing is nonthrowing and withdraws undispatched
 * access; it never deletes a document or claims to undo an already dispatched deletion.
 */
interface BackupDocumentCopy : Closeable {
    /** Untrusted presentation only, never an identifier or deletion input. Null for an export. */
    val displayName: String? get() = null

    suspend fun deleteAndVerify(): BackupDocumentTransfer.DeleteResult
    override fun close()
}
