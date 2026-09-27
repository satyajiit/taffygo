// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.app.Activity;
import android.content.Intent;
import android.content.IntentSender;
import android.os.Bundle;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.ui.base.IntentRequestTracker;
import org.chromium.ui.base.WindowAndroid;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

/** Product-level proof of the upstream tracker lifecycle that file selection relies on. */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyFileChooserLifecycleTest {
    @Test
    public void processRecreationRestoresOnlyAnErrorAndNeverAUriCallback() {
        TrackerDelegate beforeDeath = new TrackerDelegate(true);
        IntentRequestTracker original = IntentRequestTracker.createFromDelegate(beforeDeath);
        AtomicInteger callbacks = new AtomicInteger();
        int requestCode =
                original.showCancelableIntent(
                        new Intent(Intent.ACTION_GET_CONTENT),
                        (code, data) -> callbacks.incrementAndGet(),
                        android.R.string.cancel);
        Bundle saved = new Bundle();
        original.saveInstanceState(saved);

        TrackerDelegate afterDeath = new TrackerDelegate(true);
        IntentRequestTracker recreated = IntentRequestTracker.createFromDelegate(afterDeath);
        recreated.restoreInstanceState(saved);
        Intent lateResult = new Intent().setData(UriForTest.CONTENT_URI);

        assertFalse(recreated.onActivityResult(requestCode, Activity.RESULT_OK, lateResult));
        assertEquals(0, callbacks.get());
        assertEquals(1, afterDeath.missingCallbackErrors.size());
        assertTrue(!afterDeath.missingCallbackErrors.get(0).isEmpty());
    }

    @Test
    public void failedPlatformLaunchStoresNoCallback() {
        TrackerDelegate unavailable = new TrackerDelegate(false);
        IntentRequestTracker tracker = IntentRequestTracker.createFromDelegate(unavailable);
        AtomicInteger callbacks = new AtomicInteger();

        int requestCode =
                tracker.showCancelableIntent(
                        new Intent(Intent.ACTION_GET_CONTENT),
                        (code, data) -> callbacks.incrementAndGet(),
                        android.R.string.cancel);

        assertEquals(WindowAndroid.START_INTENT_FAILURE, requestCode);
        assertEquals(0, callbacks.get());
    }

    private static final class UriForTest {
        private static final android.net.Uri CONTENT_URI =
                android.net.Uri.parse("content://provider/not-restored");
    }

    private static final class TrackerDelegate implements IntentRequestTracker.Delegate {
        private final boolean starts;
        private final List<String> missingCallbackErrors = new ArrayList<>();

        TrackerDelegate(boolean starts) {
            this.starts = starts;
        }

        @Override
        public boolean startActivityForResult(Intent intent, int requestCode) {
            return starts;
        }

        @Override
        public boolean startIntentSenderForResult(IntentSender intentSender, int requestCode) {
            return starts;
        }

        @Override
        public void finishActivity(int requestCode) {}

        @Override
        public WeakReference<Activity> getActivity() {
            return new WeakReference<>(null);
        }

        @Override
        public boolean onCallbackNotFoundError(String error) {
            missingCallbackErrors.add(error);
            return true;
        }
    }
}
