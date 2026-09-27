// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.os.SystemClock;

import androidx.annotation.VisibleForTesting;

import java.util.function.LongSupplier;

/** One short-lived, tab-bound proof that Android delivered a physical page gesture. */
final class TaffyManualGestureGate {
    @VisibleForTesting static final long MAX_AGE_MILLIS = 2_000;

    private final LongSupplier mClock;
    private int mTabId = -1;
    private long mOccurredAtMillis;
    private boolean mAvailable;
    private boolean mClosed;

    TaffyManualGestureGate() {
        this(SystemClock::uptimeMillis);
    }

    @VisibleForTesting
    TaffyManualGestureGate(LongSupplier clock) {
        mClock = clock;
    }

    /**
     * Replaces any unspent proof; only the most recent physical page gesture can authorize work.
     */
    void record(int tabId, long occurredAtMillis) {
        if (mClosed || tabId < 0 || occurredAtMillis < 0) return;
        mTabId = tabId;
        mOccurredAtMillis = occurredAtMillis;
        mAvailable = true;
    }

    /** Claims the proof exactly once and fails closed for a stale, cross-tab, or closed claim. */
    boolean consume(int tabId) {
        if (mClosed || !mAvailable) return false;
        mAvailable = false;
        long ageMillis = mClock.getAsLong() - mOccurredAtMillis;
        return tabId == mTabId && ageMillis >= 0 && ageMillis <= MAX_AGE_MILLIS;
    }

    void clear() {
        mAvailable = false;
    }

    void close() {
        mClosed = true;
        mAvailable = false;
    }
}
