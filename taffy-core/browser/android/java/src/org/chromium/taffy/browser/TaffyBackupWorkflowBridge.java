// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import androidx.annotation.AnyThread;
import androidx.annotation.Nullable;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;

import java.io.Closeable;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Serialized Java custody for one regular profile's native backup workflow. */
public final class TaffyBackupWorkflowBridge implements Closeable {
    public static final int MODE_CREATE = 0;
    public static final int MODE_RESTORE = 1;
    public static final int ACCEPTED = 0;
    public static final int REFUSED = 1;
    public static final int UNAVAILABLE = 2;
    public static final int PREPARED = 0;

    private static final int MAX_SELECTION = 6;
    private static final int MAX_PENDING_PREPARATIONS = 4;

    /** Exact product-window handle. Its opaque operation ids never leave the host adapter. */
    public static final class Window implements Closeable {
        private final TaffyBackupWorkflowBridge mOwner;
        private final long mToken;
        private final TaffyBackupRestoreWindow mRestore;
        private final Map<String, Callback<TaffyBackupExportPreparation>> mPending =
                new LinkedHashMap<>();
        private boolean mClosed;

        private Window(TaffyBackupWorkflowBridge owner, long token) {
            mOwner = owner;
            mToken = token;
            mRestore = new TaffyBackupRestoreWindow(this);
        }

        public TaffyBackupRestoreWindow restore() {
            return mRestore;
        }

        public boolean activate() {
            ThreadUtils.assertOnUiThread();
            synchronized (mOwner.mLock) {
                return isUsableLocked()
                        && TaffyBackupWorkflowBridgeJni.get()
                                .activate(mOwner.mNativeBridge, mToken);
            }
        }

        public void deactivate() {
            ThreadUtils.assertOnUiThread();
            synchronized (mOwner.mLock) {
                if (isUsableLocked()) {
                    TaffyBackupWorkflowBridgeJni.get()
                            .deactivate(mOwner.mNativeBridge, mToken);
                }
            }
        }

        public @Nullable String beginRecoveryKeySession(int mode) {
            ThreadUtils.assertOnUiThread();
            synchronized (mOwner.mLock) {
                if (!isUsableLocked() || (mode != MODE_CREATE && mode != MODE_RESTORE)) {
                    return null;
                }
                return TaffyBackupWorkflowBridgeJni.get()
                        .beginRecoveryKeySession(mOwner.mNativeBridge, mToken, mode);
            }
        }

        public @Nullable char[] takeGeneratedKeyForDisplay(String operationId) {
            ThreadUtils.assertOnUiThread();
            synchronized (mOwner.mLock) {
                return isUsableLocked()
                        ? TaffyBackupWorkflowBridgeJni.get()
                                .takeGeneratedKeyForDisplay(
                                        mOwner.mNativeBridge, mToken, operationId)
                        : null;
            }
        }

        public int confirmKeyRetained(String operationId) {
            ThreadUtils.assertOnUiThread();
            synchronized (mOwner.mLock) {
                return isUsableLocked()
                        ? TaffyBackupWorkflowBridgeJni.get()
                                .confirmKeyRetained(mOwner.mNativeBridge, mToken, operationId)
                        : UNAVAILABLE;
            }
        }

        public int acceptEnteredKey(String operationId, char[] key) {
            ThreadUtils.assertOnUiThread();
            synchronized (mOwner.mLock) {
                return isUsableLocked()
                        ? TaffyBackupWorkflowBridgeJni.get()
                                .acceptEnteredKey(mOwner.mNativeBridge, mToken, operationId, key)
                        : UNAVAILABLE;
            }
        }

        public boolean prepareExport(
                String operationId,
                int[] selection,
                Callback<TaffyBackupExportPreparation> callback) {
            ThreadUtils.assertOnUiThread();
            if (callback == null
                    || selection == null
                    || selection.length == 0
                    || selection.length > MAX_SELECTION) {
                return false;
            }
            synchronized (mOwner.mLock) {
                if (!isUsableLocked()
                        || mPending.size() >= MAX_PENDING_PREPARATIONS
                        || mPending.putIfAbsent(operationId, callback) != null) {
                    return false;
                }
                boolean admitted =
                        TaffyBackupWorkflowBridgeJni.get()
                                .prepareExport(
                                        mOwner.mNativeBridge,
                                        mToken,
                                        operationId,
                                        selection);
                if (!admitted) mPending.remove(operationId);
                return admitted;
            }
        }

        public long maximumImportBytes(String operationId) {
            synchronized (mOwner.mLock) {
                return isUsableLocked()
                        ? TaffyBackupWorkflowBridgeJni.get()
                                .maximumImportBytes(mOwner.mNativeBridge, mToken, operationId)
                        : -1;
            }
        }

        public int openEncryptedArchiveReadFd(String operationId) {
            synchronized (mOwner.mLock) {
                return isUsableLocked()
                        ? TaffyBackupWorkflowBridgeJni.get()
                                .openEncryptedArchiveReadFd(
                                        mOwner.mNativeBridge, mToken, operationId)
                        : -1;
            }
        }

        public int openReadbackWriteFd(String operationId, long expectedBytes) {
            synchronized (mOwner.mLock) {
                return isUsableLocked()
                        ? TaffyBackupWorkflowBridgeJni.get()
                                .openReadbackWriteFd(
                                        mOwner.mNativeBridge,
                                        mToken,
                                        operationId,
                                        expectedBytes)
                        : -1;
            }
        }

        public int verifyEncryptedReadback(String operationId) {
            // Acquire and complete under the owner monitor; hash outside it.
            return TaffyBackupArchiveIo.run(
                    mOwner.mLock,
                    () ->
                            isUsableLocked()
                                    ? TaffyBackupWorkflowBridgeJni.get()
                                            .acquireExportVerification(
                                                    mOwner.mNativeBridge, mToken, operationId)
                                    : 0,
                    ioHandle ->
                            isUsableLocked()
                                    ? TaffyBackupWorkflowBridgeJni.get()
                                            .completeExportVerification(
                                                    mOwner.mNativeBridge,
                                                    mToken,
                                                    operationId,
                                                    ioHandle)
                                    : UNAVAILABLE);
        }

        public int openEncryptedImportWriteFd(String operationId, long maximumBytes) {
            synchronized (mOwner.mLock) {
                return isUsableLocked()
                        ? TaffyBackupWorkflowBridgeJni.get()
                                .openEncryptedImportWriteFd(
                                        mOwner.mNativeBridge,
                                        mToken,
                                        operationId,
                                        maximumBytes)
                        : -1;
            }
        }

        public int inspectImportedArchive(String operationId, long actualBytes) {
            // Acquire and complete under the owner monitor; inspect outside it.
            return TaffyBackupArchiveIo.run(
                    mOwner.mLock,
                    () ->
                            isUsableLocked()
                                    ? TaffyBackupWorkflowBridgeJni.get()
                                            .acquireImportInspection(
                                                    mOwner.mNativeBridge,
                                                    mToken,
                                                    operationId,
                                                    actualBytes)
                                    : 0,
                    ioHandle ->
                            isUsableLocked()
                                    ? TaffyBackupWorkflowBridgeJni.get()
                                            .completeImportInspection(
                                                    mOwner.mNativeBridge,
                                                    mToken,
                                                    operationId,
                                                    ioHandle)
                                    : UNAVAILABLE);
        }

        /**
         * Withdraws one operation from any caller thread. Document I/O cleanup
         * reaches this method from a worker; marshal before taking the owner
         * monitor because native workflow and restore custody are UI-owned.
         */
        @AnyThread
        public void abandon(String operationId) {
            if (ThreadUtils.runningOnUiThread()) {
                abandonOnUiThread(operationId);
                return;
            }
            ThreadUtils.runOnUiThreadBlocking(() -> abandonOnUiThread(operationId));
        }

        private void abandonOnUiThread(String operationId) {
            ThreadUtils.assertOnUiThread();
            Callback<TaffyBackupExportPreparation> callback;
            synchronized (mOwner.mLock) {
                callback = mPending.remove(operationId);
                if (isUsableLocked()) {
                    TaffyBackupWorkflowBridgeJni.get()
                            .abandonOperation(mOwner.mNativeBridge, mToken, operationId);
                }
            }
            if (callback != null) callback.onResult(TaffyBackupExportPreparation.unavailable());
        }

        @Override
        public void close() {
            ThreadUtils.assertOnUiThread();
            List<Callback<TaffyBackupExportPreparation>> callbacks = new ArrayList<>();
            List<Runnable> restoreSettlements = new ArrayList<>();
            synchronized (mOwner.mLock) {
                closeLocked(callbacks, restoreSettlements);
            }
            settleUnavailable(callbacks);
            restoreSettlements.forEach(Runnable::run);
        }

        long nativeBridgeForRestore() {
            synchronized (mOwner.mLock) {
                return isUsableLocked() ? mOwner.mNativeBridge : 0;
            }
        }

        long tokenForRestore() {
            return mToken;
        }

        private boolean isUsableLocked() {
            return !mClosed && mOwner.mNativeBridge != 0;
        }

        private void closeLocked(
                List<Callback<TaffyBackupExportPreparation>> callbacks,
                List<Runnable> restoreSettlements) {
            if (mClosed) return;
            mClosed = true;
            mRestore.closeLocked(restoreSettlements);
            callbacks.addAll(mPending.values());
            mPending.clear();
            if (mOwner.mNativeBridge != 0) {
                TaffyBackupWorkflowBridgeJni.get()
                        .unregisterWindow(mOwner.mNativeBridge, mToken);
            }
            mOwner.mWindows.remove(mToken);
        }

        private @Nullable Callback<TaffyBackupExportPreparation> takePreparation(
                String operationId) {
            return mPending.remove(operationId);
        }
    }

    private final Object mLock = new Object();
    private final Map<Long, Window> mWindows = new LinkedHashMap<>();
    private long mNativeBridge;

    /** Opens only for the current non-quarantined original regular profile. */
    public static @Nullable TaffyBackupWorkflowBridge open(Profile profile) {
        ThreadUtils.assertOnUiThread();
        TaffyBackupWorkflowBridge bridge = new TaffyBackupWorkflowBridge();
        bridge.mNativeBridge = TaffyBackupWorkflowBridgeJni.get().init(bridge, profile);
        return bridge.mNativeBridge != 0 ? bridge : null;
    }

    private TaffyBackupWorkflowBridge() {}

    public @Nullable Window openWindow() {
        ThreadUtils.assertOnUiThread();
        synchronized (mLock) {
            if (mNativeBridge == 0) return null;
            long token = TaffyBackupWorkflowBridgeJni.get().registerWindow(mNativeBridge);
            if (token <= 0 || mWindows.containsKey(token)) return null;
            Window window = new Window(this, token);
            mWindows.put(token, window);
            return window;
        }
    }

    @Override
    public void close() {
        ThreadUtils.assertOnUiThread();
        List<Callback<TaffyBackupExportPreparation>> callbacks = new ArrayList<>();
        List<Runnable> restoreSettlements = new ArrayList<>();
        synchronized (mLock) {
            if (mNativeBridge == 0) return;
            for (Window window : new ArrayList<>(mWindows.values())) {
                window.closeLocked(callbacks, restoreSettlements);
            }
            long nativeBridge = mNativeBridge;
            mNativeBridge = 0;
            TaffyBackupWorkflowBridgeJni.get().destroy(nativeBridge);
        }
        settleUnavailable(callbacks);
        restoreSettlements.forEach(Runnable::run);
    }

    @CalledByNative
    private void onExportPrepared(
            long windowToken, String operationId, long archiveBytes, int status) {
        Callback<TaffyBackupExportPreparation> callback;
        TaffyBackupExportPreparation result;
        synchronized (mLock) {
            Window window = mWindows.get(windowToken);
            callback = window != null ? window.takePreparation(operationId) : null;
            result = TaffyBackupExportPreparation.validated(archiveBytes, status);
        }
        if (callback != null) callback.onResult(result);
    }

    private static void settleUnavailable(
            List<Callback<TaffyBackupExportPreparation>> callbacks) {
        for (Callback<TaffyBackupExportPreparation> callback : callbacks) {
            callback.onResult(TaffyBackupExportPreparation.unavailable());
        }
    }

    @NativeMethods
    interface Natives {
        long init(TaffyBackupWorkflowBridge caller, @JniType("Profile*") Profile profile);
        void destroy(long bridgePtr);
        long registerWindow(long bridgePtr);
        void unregisterWindow(long bridgePtr, long windowToken);
        boolean activate(long bridgePtr, long windowToken);
        void deactivate(long bridgePtr, long windowToken);
        @Nullable String beginRecoveryKeySession(long bridgePtr, long windowToken, int mode);
        @Nullable char[] takeGeneratedKeyForDisplay(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId);
        int confirmKeyRetained(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId);
        int acceptEnteredKey(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId,
                char[] key);
        boolean prepareExport(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId,
                @JniType("std::vector<int32_t>") int[] selection);
        long maximumImportBytes(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId);
        int openEncryptedArchiveReadFd(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId);
        int openReadbackWriteFd(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId,
                long expectedBytes);
        long acquireExportVerification(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId);
        int openEncryptedImportWriteFd(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId,
                long maximumBytes);
        long acquireImportInspection(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId,
                long actualBytes);
        void runArchiveIo(long ioHandle);
        int completeExportVerification(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId,
                long ioHandle);
        int completeImportInspection(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId,
                long ioHandle);
        void destroyArchiveIo(long ioHandle);
        void abandonOperation(
                long bridgePtr,
                long windowToken,
                @JniType("std::string") String operationId);
    }
}
