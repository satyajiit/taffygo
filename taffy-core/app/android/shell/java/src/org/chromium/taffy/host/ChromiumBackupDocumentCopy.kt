// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.net.Uri
import com.taffygo.browser.ui.app.AndroidBackupDocumentAdapter
import com.taffygo.browser.ui.app.BackupDocumentCopy
import com.taffygo.browser.ui.app.BackupDocumentTransfer

/** Spent once, after transfer drain or fresh selection; never exports its URI or retains a key. */
internal class ChromiumBackupDocumentCopy(
    private val window: ChromiumBackupWindowState,
    private val documents: AndroidBackupDocumentAdapter,
    document: Uri,
    override val displayName: String? = null,
) : BackupDocumentCopy {
    private val lock = Any()
    private var document: Uri? = document
    private var closed = false

    override suspend fun deleteAndVerify(): BackupDocumentTransfer.DeleteResult {
        val selected = synchronized(lock) {
            if (closed) null else document.also { document = null }
        } ?: return BackupDocumentTransfer.DeleteResult.UNVERIFIABLE
        val incarnation = synchronized(window.lock) {
            if (window.closed || !window.active) null else window.activityIncarnation
        }
        return try {
            documents.deleteAndVerify(selected) {
                // Check at the actual IO dispatch, not before waiting for its dispatcher.
                synchronized(lock) {
                    !closed && incarnation != null && synchronized(window.lock) {
                        !window.closed && window.active && window.activityIncarnation === incarnation
                    }
                }
            }
        } finally {
            close()
        }
    }

    override fun close() {
        synchronized(lock) {
            closed = true
            document = null
        }
        synchronized(window.lock) { window.documentCopies.remove(this) }
    }
}
