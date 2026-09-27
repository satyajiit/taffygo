// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Owns restart-only discovery and resolution callbacks for one exact product window. */
final class TaffyBackupRestartRecovery {
    private static final int MAX_PENDING = 4;
    private static final int FIELDS_PER_CLASS = 7;

    private static final class PendingDiscovery {
        final TaffyBackupRestoreWindow.InterruptedRestoreDiscoveryRequest request;
        final Callback<TaffyInterruptedRestoreDiscovery> callback;

        PendingDiscovery(
                TaffyBackupRestoreWindow.InterruptedRestoreDiscoveryRequest request,
                Callback<TaffyInterruptedRestoreDiscovery> callback) {
            this.request = request;
            this.callback = callback;
        }
    }

    private final TaffyBackupRestoreWindow mCaller;
    private final TaffyBackupWorkflowBridge.Window mWindow;
    private final Map<Long, PendingDiscovery> mDiscoveries = new LinkedHashMap<>();
    private final Map<Long, Callback<Integer>> mResolutions = new LinkedHashMap<>();
    private long mNextRequestToken = 1;
    private boolean mClosed;

    TaffyBackupRestartRecovery(
            TaffyBackupRestoreWindow caller, TaffyBackupWorkflowBridge.Window window) {
        mCaller = caller;
        mWindow = window;
    }

    TaffyBackupRestoreWindow.InterruptedRestoreDiscoveryRequest discover(
            Callback<TaffyInterruptedRestoreDiscovery> callback, int totalPending) {
        ThreadUtils.assertOnUiThread();
        long nativeBridge = mWindow.nativeBridgeForRestore();
        long requestToken = nextRequestToken();
        if (callback == null
                || nativeBridge == 0
                || requestToken <= 0
                || mClosed
                || totalPending >= MAX_PENDING) {
            return null;
        }
        TaffyBackupRestoreWindow.InterruptedRestoreDiscoveryRequest request =
                new TaffyBackupRestoreWindow.InterruptedRestoreDiscoveryRequest(
                        this, requestToken);
        PendingDiscovery pending = new PendingDiscovery(request, callback);
        mDiscoveries.put(requestToken, pending);
        boolean admitted =
                TaffyBackupRestoreWindowJni.get()
                        .discoverInterruptedRestore(
                                mCaller,
                                nativeBridge,
                                mWindow.tokenForRestore(),
                                requestToken);
        if (!admitted && mDiscoveries.remove(requestToken, pending)) {
            request.markSettled();
            return null;
        }
        return request;
    }

    boolean resolve(
            long recoveredReviewToken,
            int choice,
            Callback<Integer> callback,
            int totalPending) {
        ThreadUtils.assertOnUiThread();
        long nativeBridge = mWindow.nativeBridgeForRestore();
        if (callback == null
                || recoveredReviewToken <= 0
                || (choice != TaffyBackupRestoreWindow.RESTORE_ACCEPT
                        && choice != TaffyBackupRestoreWindow.RESTORE_DISCARD)
                || nativeBridge == 0
                || mClosed
                || totalPending >= MAX_PENDING
                || mResolutions.putIfAbsent(recoveredReviewToken, callback) != null) {
            return false;
        }
        boolean admitted =
                TaffyBackupRestoreWindowJni.get()
                        .resolveRecoveredRestore(
                                mCaller,
                                nativeBridge,
                                mWindow.tokenForRestore(),
                                recoveredReviewToken,
                                choice);
        if (!admitted) mResolutions.remove(recoveredReviewToken, callback);
        return admitted;
    }

    void abandonReview(long recoveredReviewToken) {
        ThreadUtils.assertOnUiThread();
        if (recoveredReviewToken <= 0) return;
        mResolutions.remove(recoveredReviewToken);
        abandonRecoveredToken(recoveredReviewToken);
    }

    void close(TaffyBackupRestoreWindow.InterruptedRestoreDiscoveryRequest request) {
        ThreadUtils.assertOnUiThread();
        if (request.mSettled) return;
        PendingDiscovery pending = mDiscoveries.get(request.mRequestToken);
        if (pending == null || pending.request != request) {
            request.markSettled();
            return;
        }
        mDiscoveries.remove(request.mRequestToken);
        request.markSettled();
        long nativeBridge = mWindow.nativeBridgeForRestore();
        if (nativeBridge != 0) {
            TaffyBackupRestoreWindowJni.get()
                    .abandonInterruptedRestoreDiscovery(
                            nativeBridge, mWindow.tokenForRestore(), request.mRequestToken);
        }
    }

    void closeLocked(List<Runnable> settlements) {
        ThreadUtils.assertOnUiThread();
        if (mClosed) return;
        mClosed = true;
        for (PendingDiscovery pending : mDiscoveries.values()) {
            pending.request.markSettled();
        }
        for (Callback<Integer> callback : mResolutions.values()) {
            settlements.add(
                    () ->
                            callback.onResult(
                                    TaffyBackupRestoreWindow.RESTORE_RESOLUTION_RECOVERY_REQUIRED));
        }
        mDiscoveries.clear();
        mResolutions.clear();
    }

    void onDiscovered(
            long requestToken,
            long recoveredReviewToken,
            String targetProfileLabel,
            int[] flattenedClassCounts,
            boolean hasConflicts,
            boolean canStage,
            boolean cleanupOnly,
            int status) {
        ThreadUtils.assertOnUiThread();
        PendingDiscovery pending = mDiscoveries.remove(requestToken);
        if (pending == null) {
            if (recoveredReviewToken > 0) abandonRecoveredToken(recoveredReviewToken);
            return;
        }
        pending.request.markSettled();
        boolean actionable =
                status == TaffyBackupRestoreWindow.DISCOVERY_ROLLBACK_AVAILABLE
                        || status == TaffyBackupRestoreWindow.DISCOVERY_CLEANUP_REQUIRED;
        boolean validAction =
                actionable
                        && recoveredReviewToken > 0
                        && targetProfileLabel != null
                        && !targetProfileLabel.isEmpty()
                        && flattenedClassCounts != null
                        && flattenedClassCounts.length >= FIELDS_PER_CLASS
                        && flattenedClassCounts.length <= 6 * FIELDS_PER_CLASS
                        && flattenedClassCounts.length % FIELDS_PER_CLASS == 0
                        && cleanupOnly
                                == (status
                                        == TaffyBackupRestoreWindow.DISCOVERY_CLEANUP_REQUIRED);
        boolean validObservation =
                !actionable
                        && status >= TaffyBackupRestoreWindow.DISCOVERY_NONE
                        && status <= TaffyBackupRestoreWindow.DISCOVERY_UNAVAILABLE
                        && recoveredReviewToken == 0
                        && targetProfileLabel != null
                        && targetProfileLabel.isEmpty()
                        && flattenedClassCounts != null
                        && flattenedClassCounts.length == 0
                        && !hasConflicts
                        && !canStage
                        && !cleanupOnly;
        if (!validAction && !validObservation) {
            if (recoveredReviewToken > 0) abandonRecoveredToken(recoveredReviewToken);
            pending.callback.onResult(unavailableDiscovery());
            return;
        }
        pending.callback.onResult(
                new TaffyInterruptedRestoreDiscovery(
                        recoveredReviewToken,
                        targetProfileLabel,
                        flattenedClassCounts,
                        hasConflicts,
                        canStage,
                        cleanupOnly,
                        status));
    }

    void onResolved(long recoveredReviewToken, int status) {
        ThreadUtils.assertOnUiThread();
        Callback<Integer> callback = mResolutions.remove(recoveredReviewToken);
        if (callback != null) callback.onResult(status);
    }

    int pendingCount() {
        return mDiscoveries.size() + mResolutions.size();
    }

    private long nextRequestToken() {
        if (mClosed) return 0;
        for (int attempt = 0; attempt <= MAX_PENDING; ++attempt) {
            long candidate = mNextRequestToken;
            mNextRequestToken = candidate == Long.MAX_VALUE ? 1 : candidate + 1;
            if (candidate > 0 && !mDiscoveries.containsKey(candidate)) return candidate;
        }
        return 0;
    }

    private void abandonRecoveredToken(long recoveredReviewToken) {
        long nativeBridge = mWindow.nativeBridgeForRestore();
        if (nativeBridge != 0) {
            TaffyBackupRestoreWindowJni.get()
                    .abandonRecoveredRestoreReview(
                            nativeBridge, mWindow.tokenForRestore(), recoveredReviewToken);
        }
    }

    private static TaffyInterruptedRestoreDiscovery unavailableDiscovery() {
        return new TaffyInterruptedRestoreDiscovery(
                0, "", new int[0], false, false, false,
                TaffyBackupRestoreWindow.DISCOVERY_UNAVAILABLE);
    }
}
