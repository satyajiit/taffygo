// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import java.util.Arrays;

/** Content-free UI projection of one native-held restore plan. */
public final class TaffyBackupRestorePreparation {
    private final long mReviewToken;
    private final String mTargetProfileLabel;
    private final int[] mFlattenedClassCounts;
    private final boolean mHasConflicts;
    private final boolean mCanStage;
    private final int mStatus;

    TaffyBackupRestorePreparation(
            long reviewToken,
            String targetProfileLabel,
            int[] flattenedClassCounts,
            boolean hasConflicts,
            boolean canStage,
            int status) {
        mReviewToken = reviewToken;
        mTargetProfileLabel = targetProfileLabel;
        mFlattenedClassCounts = Arrays.copyOf(flattenedClassCounts, flattenedClassCounts.length);
        mHasConflicts = hasConflicts;
        mCanStage = canStage;
        mStatus = status;
    }

    public long reviewToken() {
        return mReviewToken;
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

    public int status() {
        return mStatus;
    }
}
