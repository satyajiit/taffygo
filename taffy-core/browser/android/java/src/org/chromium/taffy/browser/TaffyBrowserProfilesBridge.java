// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import androidx.annotation.Nullable;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** The narrow Android bridge for regular browser-profile lifecycle operations. */
public final class TaffyBrowserProfilesBridge {
    public static final int ERROR_NONE = 0;
    public static final int ERROR_UNAVAILABLE = 1;
    public static final int ERROR_PRIVATE_PROFILE = 2;
    public static final int ERROR_NOT_ACTIVE = 3;
    public static final int ERROR_INVALID_NAME = 4;
    public static final int ERROR_LIMIT_REACHED = 5;
    public static final int ERROR_DUPLICATE_NAME = 6;
    public static final int ERROR_NOT_FOUND = 7;
    public static final int ERROR_ACTIVE_PROFILE = 8;
    public static final int ERROR_LAST_PROFILE = 9;
    public static final int ERROR_BUSY = 10;
    public static final int ERROR_OPERATION_FAILED = 11;
    public static final int ERROR_PROFILE_IN_USE = 12;

    private static final int PROFILE_FIELD_COUNT = 4;
    private static final int MAX_PENDING_OPERATIONS = 8;
    private static final Map<Long, Callback<OperationResult>> sPending = new LinkedHashMap<>();
    private static long sNextRequestId = 1;

    private TaffyBrowserProfilesBridge() {}

    /** A path-free profile projection safe to hand to product UI. */
    public static final class ProfileSummary {
        private final String mId;
        private final String mDisplayName;
        private final boolean mActive;
        private final boolean mUsesDefaultName;

        private ProfileSummary(
                String id, String displayName, boolean active, boolean usesDefaultName) {
            mId = id;
            mDisplayName = displayName;
            mActive = active;
            mUsesDefaultName = usesDefaultName;
        }

        public String id() {
            return mId;
        }

        public String displayName() {
            return mDisplayName;
        }

        public boolean active() {
            return mActive;
        }

        /**
         * Whether Chromium chose {@link #displayName()} rather than a person. The first
         * profile is named "Your Chromium" by the engine; product UI shows its own name
         * for it instead (decision 0255).
         */
        public boolean usesDefaultName() {
            return mUsesDefaultName;
        }
    }

    /** Completion for create, activate, and delete. A successful delete has no profile. */
    public static final class OperationResult {
        private final @Nullable Profile mProfile;
        private final int mError;

        private OperationResult(@Nullable Profile profile, int error) {
            mProfile = profile;
            mError = error;
        }

        public @Nullable Profile profile() {
            return mProfile;
        }

        public int error() {
            return mError;
        }

        public boolean succeeded() {
            return mError == ERROR_NONE;
        }
    }

    /** Lists only durable, regular profiles. Filesystem paths never cross this seam. */
    public static List<ProfileSummary> listProfiles(Profile currentProfile) {
        ThreadUtils.assertOnUiThread();
        List<String> fields = TaffyBrowserProfilesBridgeJni.get().listProfiles(currentProfile);
        if (fields.size() % PROFILE_FIELD_COUNT != 0) return Collections.emptyList();

        List<ProfileSummary> profiles = new ArrayList<>(fields.size() / PROFILE_FIELD_COUNT);
        Set<String> ids = new HashSet<>();
        for (int index = 0; index < fields.size(); index += PROFILE_FIELD_COUNT) {
            String id = fields.get(index);
            String displayName = fields.get(index + 1);
            String active = fields.get(index + 2);
            String defaultName = fields.get(index + 3);
            if (id.isEmpty()
                    || displayName.isEmpty()
                    || !(active.equals("0") || active.equals("1"))
                    || !(defaultName.equals("0") || defaultName.equals("1"))
                    || !ids.add(id)) {
                return Collections.emptyList();
            }
            profiles.add(
                    new ProfileSummary(
                            id, displayName, active.equals("1"), defaultName.equals("1")));
        }
        return Collections.unmodifiableList(profiles);
    }

    /** Registers one live browser window against its original regular profile. */
    public static long registerWindowLease(Profile windowProfile) {
        ThreadUtils.assertOnUiThread();
        return TaffyBrowserProfilesBridgeJni.get().registerWindowLease(windowProfile);
    }

    /** Releases a lease returned by {@link #registerWindowLease(Profile)}. */
    public static void unregisterWindowLease(long leaseId) {
        ThreadUtils.assertOnUiThread();
        if (leaseId > 0) {
            TaffyBrowserProfilesBridgeJni.get().unregisterWindowLease(leaseId);
        }
    }

    /** Creates and activates a new regular profile after native validates the exact caller. */
    public static void createProfile(
            Profile currentProfile, String displayName, Callback<OperationResult> callback) {
        startOperation(
                callback,
                requestId ->
                        TaffyBrowserProfilesBridgeJni.get()
                                .createProfile(currentProfile, displayName, requestId));
    }

    /** Loads and activates an existing regular profile identified by its opaque id. */
    public static void activateProfile(
            Profile currentProfile, String profileId, Callback<OperationResult> callback) {
        startOperation(
                callback,
                requestId ->
                        TaffyBrowserProfilesBridgeJni.get()
                                .activateProfile(currentProfile, profileId, requestId));
    }

    /** Deletes a non-active regular profile after trusted UI has confirmed the destructive step. */
    public static void deleteProfile(
            Profile currentProfile, String profileId, Callback<OperationResult> callback) {
        startOperation(
                callback,
                requestId ->
                        TaffyBrowserProfilesBridgeJni.get()
                                .deleteProfile(currentProfile, profileId, requestId));
    }

    private interface NativeOperation {
        void start(long requestId);
    }

    private static void startOperation(
            Callback<OperationResult> callback, NativeOperation operation) {
        ThreadUtils.assertOnUiThread();
        if (callback == null) return;
        if (sPending.size() >= MAX_PENDING_OPERATIONS) {
            callback.onResult(new OperationResult(null, ERROR_BUSY));
            return;
        }
        long requestId = nextRequestId();
        sPending.put(requestId, callback);
        try {
            operation.start(requestId);
        } catch (RuntimeException exception) {
            Callback<OperationResult> pending = sPending.remove(requestId);
            if (pending != null) {
                pending.onResult(new OperationResult(null, ERROR_UNAVAILABLE));
            }
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
    private static void onOperationCompleted(long requestId, @Nullable Profile profile, int error) {
        ThreadUtils.assertOnUiThread();
        Callback<OperationResult> callback = sPending.remove(requestId);
        if (callback != null) {
            callback.onResult(new OperationResult(profile, error));
        }
    }

    @NativeMethods
    interface Natives {
        @JniType("std::vector<std::string>")
        List<String> listProfiles(@JniType("Profile*") Profile currentProfile);

        long registerWindowLease(@JniType("Profile*") Profile windowProfile);

        void unregisterWindowLease(long leaseId);

        void createProfile(
                @JniType("Profile*") Profile currentProfile, String displayName, long requestId);

        void activateProfile(
                @JniType("Profile*") Profile currentProfile, String profileId, long requestId);

        void deleteProfile(
                @JniType("Profile*") Profile currentProfile, String profileId, long requestId);
    }
}
