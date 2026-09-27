// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.app.Activity;

import org.chromium.chrome.browser.tabmodel.MismatchedIndicesHandler;

/**
 * Resolves a restored window id that is still held by the Activity being
 * replaced.
 */
final class TaffyMismatchedIndicesHandler implements MismatchedIndicesHandler {
    @Override
    public boolean handleMismatchedIndices(Activity activityAtRequestedIndex,
            boolean isActivityInAppTasks, boolean isActivityInSameTask) {
        if (!(activityAtRequestedIndex instanceof TaffyBrowserActivity previous)) return false;
        if (!previous.isFinishing() && isActivityInAppTasks && !isActivityInSameTask) return false;

        // This is the same ownership transfer upstream's tabbed Activity performs:
        // stop the old store before TabWindowManager reassigns its id, then let
        // normal Activity teardown destroy the selector and tabs. Saving
        // unconditionally is intentional; unlike upstream Chrome, TaffyGo has no
        // startup timestamp heuristic that can prove this store is unchanged.
        previous.releaseTabStoreForWindowReassignment();
        if (!previous.isFinishing()) previous.finish();
        return true;
    }

    @Override
    public boolean skipIndexReassignment() {
        return false;
    }
}
