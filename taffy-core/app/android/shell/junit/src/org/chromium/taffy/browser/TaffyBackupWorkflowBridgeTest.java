// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.same;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowLooper;

import java.util.Objects;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public final class TaffyBackupWorkflowBridgeTest {
    private static final long NATIVE_BRIDGE = 11L;
    private static final long WINDOW_TOKEN = 17L;
    private static final String OPERATION_ID = "backup-test-operation";

    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private TaffyBackupWorkflowBridge.Natives mNative;
    @Mock private Profile mProfile;

    private ExecutorService mExecutor;
    private TaffyBackupWorkflowBridge mBridge;
    private TaffyBackupWorkflowBridge.Window mWindow;

    @Before
    public void setUp() {
        ThreadUtils.hasSubtleSideEffectsSetThreadAssertsDisabledForTesting(true);
        TaffyBackupWorkflowBridgeJni.setInstanceForTesting(mNative);
        when(mNative.init(any(), same(mProfile))).thenReturn(NATIVE_BRIDGE);
        when(mNative.registerWindow(NATIVE_BRIDGE)).thenReturn(WINDOW_TOKEN);
        when(mNative.activate(NATIVE_BRIDGE, WINDOW_TOKEN)).thenReturn(true);
        mBridge = Objects.requireNonNull(TaffyBackupWorkflowBridge.open(mProfile));
        mWindow = Objects.requireNonNull(mBridge.openWindow());
        assertTrue(mWindow.activate());
        mExecutor = Executors.newFixedThreadPool(2);
    }

    @After
    public void tearDown() {
        if (mWindow != null) mWindow.close();
        if (mBridge != null) mBridge.close();
        if (mExecutor != null) mExecutor.shutdownNow();
        TaffyBackupWorkflowBridgeJni.setInstanceForTesting(null);
    }

    @Test
    public void profileCloseDoesNotWaitForBlockedExportVerification() throws Exception {
        long ioHandle = 31L;
        CountDownLatch ioStarted = new CountDownLatch(1);
        CountDownLatch releaseIo = new CountDownLatch(1);
        when(mNative.acquireExportVerification(NATIVE_BRIDGE, WINDOW_TOKEN, OPERATION_ID))
                .thenReturn(ioHandle);
        blockArchiveIo(ioHandle, ioStarted, releaseIo);

        Future<Integer> verification =
                mExecutor.submit(() -> mWindow.verifyEncryptedReadback(OPERATION_ID));
        assertTrue(ioStarted.await(5, TimeUnit.SECONDS));
        Future<?> close = mExecutor.submit(mBridge::close);
        try {
            close.get(5, TimeUnit.SECONDS);
        } finally {
            releaseIo.countDown();
        }

        assertEquals(
                TaffyBackupWorkflowBridge.UNAVAILABLE,
                verification.get(5, TimeUnit.SECONDS).intValue());
        verify(mNative).unregisterWindow(NATIVE_BRIDGE, WINDOW_TOKEN);
        verify(mNative).destroy(NATIVE_BRIDGE);
        verify(mNative, never())
                .completeExportVerification(
                        NATIVE_BRIDGE, WINDOW_TOKEN, OPERATION_ID, ioHandle);
        verify(mNative).destroyArchiveIo(ioHandle);
    }

    @Test
    public void pauseResumeDoesNotWaitForBlockedImportInspection() throws Exception {
        long ioHandle = 37L;
        CountDownLatch ioStarted = new CountDownLatch(1);
        CountDownLatch releaseIo = new CountDownLatch(1);
        when(mNative.acquireImportInspection(
                        NATIVE_BRIDGE, WINDOW_TOKEN, OPERATION_ID, 901L))
                .thenReturn(ioHandle);
        when(mNative.completeImportInspection(
                        NATIVE_BRIDGE, WINDOW_TOKEN, OPERATION_ID, ioHandle))
                .thenReturn(TaffyBackupWorkflowBridge.ACCEPTED);
        blockArchiveIo(ioHandle, ioStarted, releaseIo);

        Future<Integer> inspection =
                mExecutor.submit(() -> mWindow.inspectImportedArchive(OPERATION_ID, 901L));
        assertTrue(ioStarted.await(5, TimeUnit.SECONDS));
        Future<Boolean> pauseResume =
                mExecutor.submit(
                        () -> {
                            mWindow.deactivate();
                            return mWindow.activate();
                        });
        try {
            assertTrue(pauseResume.get(5, TimeUnit.SECONDS));
        } finally {
            releaseIo.countDown();
        }

        assertEquals(
                TaffyBackupWorkflowBridge.ACCEPTED,
                inspection.get(5, TimeUnit.SECONDS).intValue());
        verify(mNative)
                .completeImportInspection(
                        NATIVE_BRIDGE, WINDOW_TOKEN, OPERATION_ID, ioHandle);
        verify(mNative).destroyArchiveIo(ioHandle);
    }

    @Test
    public void oversizedSelectionIsRefusedBeforeJniOrPendingCallbackCustody() {
        boolean admitted =
                mWindow.prepareExport(
                        OPERATION_ID, new int[7], ignored -> {
                            throw new AssertionError("refused preparation must not settle");
                        });

        assertFalse(admitted);
        verify(mNative, never())
                .prepareExport(
                        eq(NATIVE_BRIDGE), eq(WINDOW_TOKEN), eq(OPERATION_ID), any());
    }

    // The loop below interleaves Robolectric's main looper with a real worker
    // thread on purpose: what is under test is that the native call and the
    // pending settlement both land on the UI thread while another thread is
    // running. Thread.yield() is what lets the worker make progress between
    // looper drains, and Error Prone's ThreadPriorityCheck cannot tell that
    // apart from relying on the scheduler for correctness. Upstream suppresses
    // it the same way in base/android's CachingUmaRecorderTest.
    @Test
    @SuppressWarnings("ThreadPriorityCheck")
    public void workerAbandonMarshalsNativeAndPendingSettlementToUi() throws Exception {
        AtomicBoolean nativeRanOnUi = new AtomicBoolean();
        AtomicBoolean callbackRanOnUi = new AtomicBoolean();
        when(mNative.prepareExport(
                        eq(NATIVE_BRIDGE), eq(WINDOW_TOKEN), eq(OPERATION_ID), any()))
                .thenReturn(true);
        doAnswer(
                        invocation -> {
                            nativeRanOnUi.set(ThreadUtils.runningOnUiThread());
                            return null;
                        })
                .when(mNative)
                .abandonOperation(NATIVE_BRIDGE, WINDOW_TOKEN, OPERATION_ID);
        assertTrue(
                mWindow.prepareExport(
                        OPERATION_ID,
                        new int[] {1},
                        result -> {
                            callbackRanOnUi.set(ThreadUtils.runningOnUiThread());
                            assertEquals(TaffyBackupWorkflowBridge.UNAVAILABLE, result.status());
                        }));

        CountDownLatch workerStarted = new CountDownLatch(1);
        Future<?> withdrawal =
                mExecutor.submit(
                        () -> {
                            assertFalse(ThreadUtils.runningOnUiThread());
                            workerStarted.countDown();
                            mWindow.abandon(OPERATION_ID);
                        });
        assertTrue(workerStarted.await(5, TimeUnit.SECONDS));
        long deadlineNanos = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!withdrawal.isDone() && System.nanoTime() < deadlineNanos) {
            ShadowLooper.idleMainLooper();
            Thread.yield();
        }
        withdrawal.get(1, TimeUnit.SECONDS);

        assertTrue(nativeRanOnUi.get());
        assertTrue(callbackRanOnUi.get());
        verify(mNative).abandonOperation(NATIVE_BRIDGE, WINDOW_TOKEN, OPERATION_ID);
    }

    private void blockArchiveIo(
            long ioHandle, CountDownLatch started, CountDownLatch release) {
        doAnswer(
                        invocation -> {
                            started.countDown();
                            assertTrue(release.await(30, TimeUnit.SECONDS));
                            return null;
                        })
                .when(mNative)
                .runArchiveIo(ioHandle);
    }
}
