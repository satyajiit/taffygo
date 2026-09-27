// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.chromium.base.ThreadUtils;

/** Runs detached archive work outside the owner monitor. */
final class TaffyBackupArchiveIo {
    /** Short native-handle acquisition invoked only while the owner monitor is held. */
    @FunctionalInterface
    interface Acquire {
        long run();
    }

    /** Short live-owner completion invoked only while the owner monitor is held. */
    @FunctionalInterface
    interface Complete {
        int run(long handle);
    }

    static int run(Object ownerMonitor, Acquire acquire, Complete complete) {
        ThreadUtils.assertOnBackgroundThread();
        long handle;
        synchronized (ownerMonitor) {
            handle = acquire.run();
        }
        if (handle == 0) return TaffyBackupWorkflowBridge.UNAVAILABLE;
        try {
            TaffyBackupWorkflowBridgeJni.get().runArchiveIo(handle);
            synchronized (ownerMonitor) {
                return complete.run(handle);
            }
        } finally {
            TaffyBackupWorkflowBridgeJni.get().destroyArchiveIo(handle);
        }
    }

    private TaffyBackupArchiveIo() {}
}
