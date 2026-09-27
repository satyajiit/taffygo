// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

/** Content-free result of preparing one immutable encrypted export stage. */
public final class TaffyBackupExportPreparation {
    private final long mArchiveBytes;
    private final int mStatus;

    TaffyBackupExportPreparation(long archiveBytes, int status) {
        mArchiveBytes = archiveBytes;
        mStatus = status;
    }

    public long archiveBytes() {
        return mArchiveBytes;
    }

    public int status() {
        return mStatus;
    }

    static TaffyBackupExportPreparation validated(long archiveBytes, int status) {
        if (status == TaffyBackupWorkflowBridge.PREPARED && archiveBytes > 0) {
            return new TaffyBackupExportPreparation(archiveBytes, status);
        }
        if (status == TaffyBackupWorkflowBridge.REFUSED && archiveBytes == 0) {
            return new TaffyBackupExportPreparation(0, status);
        }
        return unavailable();
    }

    static TaffyBackupExportPreparation unavailable() {
        return new TaffyBackupExportPreparation(0, TaffyBackupWorkflowBridge.UNAVAILABLE);
    }
}
