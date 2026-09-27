// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;

import java.io.Closeable;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Restore-only asynchronous surface for one exact native backup window. */
public final class TaffyBackupRestoreWindow {
    public static final int RESTORE_PREPARED = 0;
    public static final int RESTORE_STAGED = 0;
    public static final int RESTORE_HIDDEN_CANDIDATE = 0;
    public static final int RESTORE_DEFINITELY_NOT_COMMITTED = 1;
    public static final int RESTORE_COMMIT_RECOVERY_REQUIRED = 2;
    public static final int RESTORE_COMMIT_REFUSED = 3;
    public static final int RESTORE_COMMIT_UNAVAILABLE = 4;
    public static final int RESTORE_PUBLISHED = 0;
    public static final int RESTORE_VERIFIED_DELETED = 1;
    public static final int RESTORE_DEFINITELY_NOT_COMPLETED = 2;
    public static final int RESTORE_RESOLUTION_RECOVERY_REQUIRED = 3;
    public static final int RESTORE_RESOLUTION_REFUSED = 4;
    public static final int RESTORE_RESOLUTION_UNAVAILABLE = 5;
    public static final int RESTORE_ACCEPT = 0;
    public static final int RESTORE_DISCARD = 1;
    public static final int DISCOVERY_NONE = 0;
    public static final int DISCOVERY_SOURCE_UNAVAILABLE = 1;
    public static final int DISCOVERY_PRECOMMIT = 2;
    public static final int DISCOVERY_PRESENTATION_UNAVAILABLE = 3;
    public static final int DISCOVERY_SCHEMA_MISMATCH = 4;
    public static final int DISCOVERY_OUTCOME_UNKNOWN = 5;
    public static final int DISCOVERY_CUSTODY_AMBIGUOUS = 6;
    public static final int DISCOVERY_ROLLBACK_AVAILABLE = 7;
    public static final int DISCOVERY_CLEANUP_REQUIRED = 8;
    public static final int DISCOVERY_PUBLISHED = 9;
    public static final int DISCOVERY_VERIFIED_DELETED = 10;
    public static final int DISCOVERY_UNAVAILABLE = 11;
    private static final int MAX_PENDING = 4;
    private static final int FIELDS_PER_CLASS = 7;

    /** One pending observational request. Closing it never chooses a candidate action. */
    public static final class InterruptedRestoreDiscoveryRequest implements Closeable {
        final TaffyBackupRestartRecovery mOwner;
        final long mRequestToken;
        boolean mSettled;

        InterruptedRestoreDiscoveryRequest(
                TaffyBackupRestartRecovery owner, long requestToken) {
            mOwner = owner;
            mRequestToken = requestToken;
        }

        @Override
        public void close() {
            ThreadUtils.assertOnUiThread();
            mOwner.close(this);
        }

        void markSettled() {
            mSettled = true;
        }
    }

    private final TaffyBackupWorkflowBridge.Window mWindow;
    private final Map<String, Callback<TaffyBackupRestorePreparation>> mPreparations =
            new LinkedHashMap<>();
    private final Map<Long, Callback<Integer>> mStages = new LinkedHashMap<>();
    private final Map<Long, Callback<Integer>> mCommits = new LinkedHashMap<>();
    private final Map<Long, Callback<Integer>> mResolutions = new LinkedHashMap<>();
    private final TaffyBackupRestartRecovery mRestartRecovery;
    private boolean mClosed;

    TaffyBackupRestoreWindow(TaffyBackupWorkflowBridge.Window window) {
        mWindow = window;
        mRestartRecovery = new TaffyBackupRestartRecovery(this, window);
    }

    public boolean prepareImportedRestore(
            String operationId,
            String targetProfileLabel,
            Callback<TaffyBackupRestorePreparation> callback) {
        ThreadUtils.assertOnUiThread();
        long nativeBridge = mWindow.nativeBridgeForRestore();
        if (callback == null
                || operationId == null
                || targetProfileLabel == null
                || nativeBridge == 0
                || !tryAdd(mPreparations, operationId, callback)) {
            return false;
        }
        boolean admitted =
                TaffyBackupRestoreWindowJni.get()
                        .prepareImportedRestore(
                                this,
                                nativeBridge,
                                mWindow.tokenForRestore(),
                                operationId,
                                targetProfileLabel);
        if (!admitted) mPreparations.remove(operationId);
        return admitted;
    }

    public boolean confirmAndStageRestore(long reviewToken, Callback<Integer> callback) {
        return startReviewRequest(reviewToken, callback, mStages, 0);
    }

    public boolean commitRestore(long reviewToken, Callback<Integer> callback) {
        return startReviewRequest(reviewToken, callback, mCommits, 1);
    }

    public boolean resolveRestore(long reviewToken, int choice, Callback<Integer> callback) {
        ThreadUtils.assertOnUiThread();
        long nativeBridge = mWindow.nativeBridgeForRestore();
        if (callback == null
                || reviewToken <= 0
                || (choice != RESTORE_ACCEPT && choice != RESTORE_DISCARD)
                || nativeBridge == 0
                || !tryAdd(mResolutions, reviewToken, callback)) {
            return false;
        }
        boolean admitted =
                TaffyBackupRestoreWindowJni.get()
                        .resolveRestore(
                                this,
                                nativeBridge,
                                mWindow.tokenForRestore(),
                                reviewToken,
                                choice);
        if (!admitted) mResolutions.remove(reviewToken);
        return admitted;
    }

    public InterruptedRestoreDiscoveryRequest discoverInterruptedRestore(
            Callback<TaffyInterruptedRestoreDiscovery> callback) {
        return mRestartRecovery.discover(callback, pendingCount());
    }

    public boolean resolveRecoveredRestore(
            long recoveredReviewToken, int choice, Callback<Integer> callback) {
        return mRestartRecovery.resolve(
                recoveredReviewToken, choice, callback, pendingCount());
    }

    public void abandonRecoveredRestoreReview(long recoveredReviewToken) {
        mRestartRecovery.abandonReview(recoveredReviewToken);
    }

    void closeLocked(List<Runnable> settlements) {
        ThreadUtils.assertOnUiThread();
        if (mClosed) return;
        mClosed = true;
        for (Callback<TaffyBackupRestorePreparation> callback : mPreparations.values()) {
            settlements.add(() -> callback.onResult(unavailablePreparation()));
        }
        addUnavailableSettlements(mStages, TaffyBackupWorkflowBridge.UNAVAILABLE, settlements);
        addUnavailableSettlements(
                mCommits,
                RESTORE_COMMIT_RECOVERY_REQUIRED,
                settlements);
        addUnavailableSettlements(
                mResolutions,
                RESTORE_RESOLUTION_RECOVERY_REQUIRED,
                settlements);
        mRestartRecovery.closeLocked(settlements);
        mPreparations.clear();
        mStages.clear();
        mCommits.clear();
        mResolutions.clear();
    }

    private boolean startReviewRequest(
            long token,
            Callback<Integer> callback,
            Map<Long, Callback<Integer>> pending,
            int kind) {
        ThreadUtils.assertOnUiThread();
        long nativeBridge = mWindow.nativeBridgeForRestore();
        if (callback == null
                || token <= 0
                || nativeBridge == 0
                || !tryAdd(pending, token, callback)) {
            return false;
        }
        boolean admitted =
                kind == 0
                        ? TaffyBackupRestoreWindowJni.get()
                                .confirmAndStageRestore(
                                        this,
                                        nativeBridge,
                                        mWindow.tokenForRestore(),
                                        token)
                        : TaffyBackupRestoreWindowJni.get()
                                .commitRestore(
                                        this,
                                        nativeBridge,
                                        mWindow.tokenForRestore(),
                                        token);
        if (!admitted) pending.remove(token);
        return admitted;
    }

    private <K, V> boolean tryAdd(Map<K, V> map, K key, V value) {
        if (mClosed || pendingCount() >= MAX_PENDING || map.containsKey(key)) return false;
        map.put(key, value);
        return true;
    }

    private int pendingCount() {
        return mPreparations.size()
                + mStages.size()
                + mCommits.size()
                + mResolutions.size()
                + mRestartRecovery.pendingCount();
    }

    @CalledByNative
    private void onRestorePrepared(
            String operationId,
            long reviewToken,
            String targetProfileLabel,
            int[] flattenedClassCounts,
            boolean hasConflicts,
            boolean canStage,
            int status) {
        ThreadUtils.assertOnUiThread();
        Callback<TaffyBackupRestorePreparation> callback = mPreparations.remove(operationId);
        if (callback == null) return;
        boolean ready =
                status == RESTORE_PREPARED
                        && reviewToken > 0
                        && targetProfileLabel != null
                        && !targetProfileLabel.isEmpty()
                        && flattenedClassCounts != null
                        && flattenedClassCounts.length >= FIELDS_PER_CLASS
                        && flattenedClassCounts.length <= 6 * FIELDS_PER_CLASS
                        && flattenedClassCounts.length % FIELDS_PER_CLASS == 0;
        callback.onResult(
                ready
                        ? new TaffyBackupRestorePreparation(
                                reviewToken,
                                targetProfileLabel,
                                flattenedClassCounts,
                                hasConflicts,
                                canStage,
                                status)
                        : unavailablePreparation(status));
    }

    @CalledByNative
    private void onRestoreStaged(long reviewToken, int status) {
        ThreadUtils.assertOnUiThread();
        deliver(mStages, reviewToken, status);
    }

    @CalledByNative
    private void onRestoreCommitted(long reviewToken, int status) {
        ThreadUtils.assertOnUiThread();
        deliver(mCommits, reviewToken, status);
    }

    @CalledByNative
    private void onRestoreResolved(long reviewToken, int status) {
        ThreadUtils.assertOnUiThread();
        deliver(mResolutions, reviewToken, status);
    }

    @CalledByNative
    private void onInterruptedRestoreDiscovered(
            long requestToken,
            long recoveredReviewToken,
            String targetProfileLabel,
            int[] flattenedClassCounts,
            boolean hasConflicts,
            boolean canStage,
            boolean cleanupOnly,
            int status) {
        mRestartRecovery.onDiscovered(
                requestToken,
                recoveredReviewToken,
                targetProfileLabel,
                flattenedClassCounts,
                hasConflicts,
                canStage,
                cleanupOnly,
                status);
    }

    @CalledByNative
    private void onRecoveredRestoreResolved(long recoveredReviewToken, int status) {
        mRestartRecovery.onResolved(recoveredReviewToken, status);
    }

    private static void deliver(Map<Long, Callback<Integer>> pending, long token, int status) {
        Callback<Integer> callback = pending.remove(token);
        if (callback != null) callback.onResult(status);
    }

    private static void addUnavailableSettlements(
            Map<Long, Callback<Integer>> source, int status, List<Runnable> output) {
        for (Callback<Integer> callback : source.values()) {
            output.add(callback.bind(status));
        }
    }

    private static TaffyBackupRestorePreparation unavailablePreparation() {
        return unavailablePreparation(TaffyBackupWorkflowBridge.UNAVAILABLE);
    }

    private static TaffyBackupRestorePreparation unavailablePreparation(int status) {
        return new TaffyBackupRestorePreparation(0, "", new int[0], false, false, status);
    }

    @NativeMethods
    interface Natives {
        boolean prepareImportedRestore(
                TaffyBackupRestoreWindow caller,
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId,
                @JniType("std::u16string") String targetProfileLabel);
        boolean confirmAndStageRestore(
                TaffyBackupRestoreWindow caller,
                long bridgePtr,
                long windowToken,
                long reviewToken);
        boolean commitRestore(
                TaffyBackupRestoreWindow caller,
                long bridgePtr,
                long windowToken,
                long reviewToken);
        boolean resolveRestore(
                TaffyBackupRestoreWindow caller,
                long bridgePtr,
                long windowToken,
                long reviewToken,
                int choice);
        boolean discoverInterruptedRestore(
                TaffyBackupRestoreWindow caller,
                long bridgePtr,
                long windowToken,
                long requestToken);
        void abandonInterruptedRestoreDiscovery(
                long bridgePtr, long windowToken, long requestToken);
        boolean resolveRecoveredRestore(
                TaffyBackupRestoreWindow caller,
                long bridgePtr,
                long windowToken,
                long recoveredReviewToken,
                int choice);
        void abandonRecoveredRestoreReview(
                long bridgePtr, long windowToken, long recoveredReviewToken);
    }
}
