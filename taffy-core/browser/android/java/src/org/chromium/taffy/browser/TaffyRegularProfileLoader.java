// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import androidx.annotation.Nullable;

import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Pattern;

/** Loads exactly one durable regular profile without exposing its path or Chromium token. */
public final class TaffyRegularProfileLoader {
    private static final int MAX_PENDING_LOADS = 8;
    private static final Pattern OPAQUE_TOKEN = Pattern.compile("[A-Za-z0-9_-]{43}");
    private static final Map<Long, Callback<Profile>> sPending = new LinkedHashMap<>();
    private static long sNextRequestId = 1;

    private TaffyRegularProfileLoader() {}

    /** Resolves or loads one regular profile; every refusal and load failure returns null. */
    public static void load(String opaqueToken, Callback<Profile> callback) {
        ThreadUtils.assertOnUiThread();
        if (callback == null) return;
        if (opaqueToken == null
                || !OPAQUE_TOKEN.matcher(opaqueToken).matches()
                || sPending.size() >= MAX_PENDING_LOADS) {
            callback.onResult(null);
            return;
        }
        long requestId = nextRequestId();
        if (requestId == 0) {
            callback.onResult(null);
            return;
        }
        sPending.put(requestId, callback);
        try {
            TaffyRegularProfileLoaderJni.get().load(opaqueToken, requestId);
        } catch (RuntimeException exception) {
            complete(requestId, null);
        }
    }

    private static long nextRequestId() {
        if (sNextRequestId == Long.MAX_VALUE) {
            if (!sPending.isEmpty()) return 0;
            sNextRequestId = 1;
        }
        return sNextRequestId++;
    }

    @CalledByNative
    private static void onLoadCompleted(long requestId, @Nullable Profile profile) {
        ThreadUtils.assertOnUiThread();
        complete(requestId, profile);
    }

    private static void complete(long requestId, @Nullable Profile profile) {
        Callback<Profile> callback = sPending.remove(requestId);
        if (callback == null) return;
        if (profile != null && (profile.isOffTheRecord() || profile.shutdownStarted())) {
            profile = null;
        }
        callback.onResult(profile);
    }

    @NativeMethods
    interface Natives {
        void load(String opaqueToken, long requestId);
    }
}
