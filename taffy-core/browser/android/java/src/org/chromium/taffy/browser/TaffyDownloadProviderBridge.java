// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.components.offline_items_collection.OfflineContentProvider;
import org.jni_zero.NativeMethods;
import org.jni_zero.JniType;

/** Resolves Chromium's offline-content provider for one exact regular profile. */
@NullMarked
public final class TaffyDownloadProviderBridge {
    private TaffyDownloadProviderBridge() {}

    /** Returns no provider rather than falling back to Chromium's process-global last-used one. */
    public static @Nullable OfflineContentProvider forProfile(Profile profile) {
        return TaffyDownloadProviderBridgeJni.get().getForProfile(profile);
    }

    /** Rechecks native task attribution and file safety before a person's manual Open. */
    public static boolean canOpenTaskDownload(Profile profile, String taskId, String downloadId) {
        return TaffyDownloadProviderBridgeJni.get()
                .canOpenTaskDownload(profile, taskId, downloadId);
    }

    @NativeMethods
    interface Natives {
        @Nullable OfflineContentProvider getForProfile(Profile profile);

        boolean canOpenTaskDownload(
                Profile profile,
                @JniType("std::string") String taskId,
                @JniType("std::string") String downloadId);
    }
}
