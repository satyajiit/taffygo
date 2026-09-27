// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import androidx.annotation.Nullable;

import org.chromium.chrome.browser.tabwindow.TabWindowManager;

/** Selects the requested id; {@code TabWindowManager} remains authoritative for assignment. */
final class TaffyWindowIdAllocator {
    static int requestedId(
            @Nullable Integer restoredId, TabWindowManager manager, int maxInstances) {
        if (restoredId != null) return restoredId;
        for (int candidate = 0; candidate < maxInstances; candidate++) {
            if (manager.getTabModelSelectorById(candidate) == null) return candidate;
        }
        return TabWindowManager.INVALID_WINDOW_ID;
    }

    private TaffyWindowIdAllocator() {}
}
