// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

/** Window-local custody shared by the document and restore adapters. */
class ChromiumBackupWindowState(
    val native: BackupNativeWindow?,
) {
    val lock = Any()
    val sessions = mutableSetOf<ChromiumBackupSession>()
    // The two registers below hold types that stay internal, so the members
    // that name them do too; nothing outside this module reads either one.
    internal val documentCopies = mutableSetOf<ChromiumBackupDocumentCopy>()
    internal val deletionRequests = mutableSetOf<ChromiumBackupDeletionRequest>()
    /** Restart presentation custody, deliberately separate from key-backed import sessions. */
    val recoveredRestoreClaims = mutableSetOf<Any>()
    var active = false
    // A resumed window is not permission to revive work confirmed before a pause.
    var activityIncarnation = Any()
    var closed = false

    fun closeLocked(session: ChromiumBackupSession) {
        session.phase = BackupSessionPhase.CLOSED
        session.archiveBytes = 0
        session.maximumBytes = 0
        session.restoreCapability = null
    }

    fun finishLocked(session: ChromiumBackupSession) {
        closeLocked(session)
        sessions.remove(session)
    }
}
