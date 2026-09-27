// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;

import org.jni_zero.CalledByNative;

import org.chromium.base.JniOnceCallback;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.components.offline_items_collection.LegacyHelpers;
import org.chromium.ui.base.WindowAndroid;

import java.lang.reflect.Method;
import java.util.Arrays;

import kotlin.ResultKt;
import kotlin.coroutines.Continuation;
import kotlin.coroutines.CoroutineContext;
import kotlin.coroutines.intrinsics.IntrinsicsKt;
import kotlinx.coroutines.Dispatchers;

/** Test-only entry to the shipping controller, with no substituted provider or authorization. */
public final class TaffyTaskPdfTestBridge {
    private static final String TAG = "TaffyPdfTest";
    private static final long READY_TIMEOUT_MILLIS = 10_000;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final ChromiumDownloadController mController;
    private final JniOnceCallback<Boolean> mCallback;
    private final String mTaskId;
    private final String mDownloadId;
    private final long mDeadline = SystemClock.uptimeMillis() + READY_TIMEOUT_MILLIS;
    private boolean mFinished;

    private TaffyTaskPdfTestBridge(Activity activity, Profile profile, String taskId, String guid,
            JniOnceCallback<Boolean> callback) {
        mController = new ChromiumDownloadController(activity, profile, mHandler);
        mCallback = callback;
        mTaskId = taskId;
        String namespace = LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE;
        mDownloadId = namespace.length() + ":" + namespace + guid;
    }

    @CalledByNative
    private static void start(Profile profile, WindowAndroid window, String taskId, String guid,
            JniOnceCallback<Boolean> callback) {
        ThreadUtils.assertOnUiThread();
        Activity activity = window.getActivity().get();
        if (activity == null || activity.isFinishing() || activity.isDestroyed()) {
            callback.onResult(false);
            return;
        }
        Log.i(TAG, "[taffy_test_pdf_handoff] start");
        new TaffyTaskPdfTestBridge(activity, profile, taskId, guid, callback).awaitProvider();
    }

    private void awaitProvider() {
        if (mController.getUnavailable().getValue() || SystemClock.uptimeMillis() >= mDeadline) {
            finish(false);
        } else if (mController.getComplete().getValue()) {
            open();
        } else {
            mHandler.postDelayed(this::awaitProvider, 25);
        }
    }

    private void open() {
        // DownloadId is a Kotlin value class: its public JVM method has a
        // mangled name Java cannot spell. Match only this exact suspend shape;
        // no accessibility override or alternate implementation is admitted.
        Method candidate = null;
        Class<?>[] parameters = {String.class, String.class, Continuation.class};
        for (Method method : ChromiumDownloadController.class.getMethods()) {
            if (!method.getName().startsWith("openForTask-")
                    || method.getReturnType() != Object.class
                    || !Arrays.equals(method.getParameterTypes(), parameters))
                continue;
            if (candidate != null) {
                finish(false);
                return;
            }
            candidate = method;
        }
        if (candidate == null) {
            finish(false);
            return;
        }
        Continuation<Boolean> continuation = new Continuation<>() {
            @Override
            public CoroutineContext getContext() {
                return Dispatchers.getMain().getImmediate();
            }

            @Override
            public void resumeWith(Object result) {
                settle(result);
            }
        };
        try {
            Object result = candidate.invoke(mController, mTaskId, mDownloadId, continuation);
            if (result != IntrinsicsKt.getCOROUTINE_SUSPENDED()) settle(result);
        } catch (ReflectiveOperationException | RuntimeException exception) {
            finish(false);
        }
    }

    private void settle(Object result) {
        try {
            ResultKt.throwOnFailure(result);
            finish(Boolean.TRUE.equals(result));
        } catch (RuntimeException exception) {
            finish(false);
        }
    }

    private void finish(boolean accepted) {
        ThreadUtils.assertOnUiThread();
        if (mFinished) return;
        mFinished = true;
        mController.destroy();
        Log.i(TAG, "[taffy_test_pdf_handoff] %s", accepted ? "accepted" : "refused");
        mCallback.onResult(accepted);
    }
}
