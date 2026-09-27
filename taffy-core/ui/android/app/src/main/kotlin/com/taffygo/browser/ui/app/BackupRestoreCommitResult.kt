// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

enum class BackupRestoreCommitResult {
    /** The committed profile remains hidden and awaits an explicit accept or discard choice. */
    HIDDEN_CANDIDATE,
    /** Nothing was committed; the exact review remains valid only for explicit discard cleanup. */
    DEFINITELY_NOT_COMMITTED,
    /** Durable custody remains, but the physical or source acknowledgement is ambiguous. */
    RECOVERY_REQUIRED,
    REFUSED,
    UNAVAILABLE,
}
