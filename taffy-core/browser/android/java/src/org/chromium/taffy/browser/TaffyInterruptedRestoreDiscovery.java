// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import java.util.Arrays;

/** Untrusted restart-discovery projection validated by the product host before presentation. */
public final class TaffyInterruptedRestoreDiscovery {
    private final long mRecoveredReviewToken;
    private final String mTargetProfileLabel;
    private final int[] mFlattenedClassCounts;
    private final boolean mHasConflicts;
    private final boolean mCanStage;
    private final boolean mCleanupOnly;
    private final int mStatus;

    TaffyInterruptedRestoreDiscovery(
            long recoveredReviewToken,
            String targetProfileLabel,
            int[] flattenedClassCounts,
            boolean hasConflicts,
            boolean canStage,
            boolean cleanupOnly,
            int status) {
        mRecoveredReviewToken = recoveredReviewToken;
        mTargetProfileLabel = targetProfileLabel;
        mFlattenedClassCounts = Arrays.copyOf(flattenedClassCounts, flattenedClassCounts.length);
        mHasConflicts = hasConflicts;
        mCanStage = canStage;
        mCleanupOnly = cleanupOnly;
        mStatus = status;
    }

    public long recoveredReviewToken() {
        return mRecoveredReviewToken;
    }

    public String targetProfileLabel() {
        return mTargetProfileLabel;
    }

    public int[] flattenedClassCounts() {
        return Arrays.copyOf(mFlattenedClassCounts, mFlattenedClassCounts.length);
    }

    public boolean hasConflicts() {
        return mHasConflicts;
    }

    public boolean canStage() {
        return mCanStage;
    }

    public boolean cleanupOnly() {
        return mCleanupOnly;
    }

    public int status() {
        return mStatus;
    }
}
