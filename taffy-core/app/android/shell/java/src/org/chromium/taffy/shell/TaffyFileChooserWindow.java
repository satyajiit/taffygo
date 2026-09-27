// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Parcelable;
import android.provider.MediaStore;

import androidx.annotation.Nullable;

import org.chromium.base.IntentUtils;
import org.chromium.ui.base.ActivityWindowAndroid;
import org.chromium.ui.base.IntentRequestTracker;
import org.chromium.ui.base.SelectFileDialog;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.insets.InsetObserver;

/**
 * The Android intent boundary for a page's HTML file input.
 *
 * <p>Chromium owns file selection from {@code FileSelectHelper} through {@link SelectFileDialog}:
 * MIME negotiation, multiple selection, the Android photo picker, provider {@code content://} URIs,
 * camera permissions and asynchronous display-name lookup all remain upstream. This class changes
 * only the authority and lifetime of the platform intent at the existing {@link WindowAndroid}
 * seam.
 *
 * <p>A {@link SelectFileDialog} may launch the person-facing GET_CONTENT, media-picker and capture
 * flows used by {@code <input type=file>}. File System Access API open/save/directory intents are
 * refused because CAP-BR-010 is intentionally an HTML file-input capability, not general page file
 * system authority. Other Chromium intent callbacks pass through unchanged.
 *
 * <p>The selected URI is never opened, copied, logged or kept here. Android's transient read grant
 * remains available to Chromium for the upload, while persistable and prefix flags are removed from
 * the outbound request. Downstream patch 0039 makes this atomic inside Chromium by persisting only
 * original OPEN_DOCUMENT results and only their returned read/write modes. The result callback also
 * releases any persisted mode as defense in depth for provider anomalies or a future upstream
 * rebase. The callback itself lives only in {@link IntentRequestTracker}; after process recreation
 * upstream restores the user-visible error but deliberately cannot restore the callback or its URI.
 */
class TaffyFileChooserWindow extends ActivityWindowAndroid {
    private static final int PERSISTABLE_FLAGS =
            Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION;

    @FunctionalInterface
    interface GrantReleaser {
        void release(Uri uri, int modeFlags);
    }

    TaffyFileChooserWindow(
            Context context,
            boolean listenToActivityState,
            IntentRequestTracker intentRequestTracker,
            @Nullable InsetObserver insetObserver,
            boolean occlusionTrackingAllowed) {
        super(
                context,
                listenToActivityState,
                intentRequestTracker,
                insetObserver,
                occlusionTrackingAllowed);
    }

    @Override
    public boolean showIntent(
            @Nullable Intent intent,
            @Nullable WindowAndroid.IntentCallback callback,
            @Nullable Integer errorId) {
        if (!(callback instanceof SelectFileDialog)) {
            return super.showIntent(intent, callback, errorId);
        }

        Intent prepared = prepareHtmlFileInputIntent(intent);
        if (prepared == null) return false;
        return super.showIntent(prepared, transientResult(callback), errorId);
    }

    @Override
    public int showCancelableIntent(
            Intent intent,
            @Nullable WindowAndroid.IntentCallback callback,
            @Nullable Integer errorId) {
        if (!(callback instanceof SelectFileDialog)) {
            return super.showCancelableIntent(intent, callback, errorId);
        }

        Intent prepared = prepareHtmlFileInputIntent(intent);
        if (prepared == null) return START_INTENT_FAILURE;
        return super.showCancelableIntent(prepared, transientResult(callback), errorId);
    }

    private WindowAndroid.IntentCallback transientResult(WindowAndroid.IntentCallback callback) {
        return transientResult(
                callback,
                (uri, flags) ->
                        getApplicationContext()
                                .getContentResolver()
                                .releasePersistableUriPermission(uri, flags));
    }

    static WindowAndroid.IntentCallback transientResult(
            WindowAndroid.IntentCallback callback, GrantReleaser releaser) {
        return (resultCode, data) -> {
            try {
                callback.onIntentCompleted(resultCode, data);
            } finally {
                releasePersistedResults(data, releaser);
            }
        };
    }

    /** Returns a detached, non-persistable intent, or {@code null} for an out-of-scope flow. */
    static @Nullable Intent prepareHtmlFileInputIntent(@Nullable Intent intent) {
        if (intent == null) return null;

        String action = intent.getAction();
        if (Intent.ACTION_CHOOSER.equals(action)) {
            return prepareChooser(intent);
        }
        if (!isHtmlFileInputAction(action)) return null;
        return withoutPersistableFlags(intent);
    }

    private static @Nullable Intent prepareChooser(Intent chooser) {
        Intent target = IntentUtils.safeGetParcelableExtra(chooser, Intent.EXTRA_INTENT);
        if (target == null || !Intent.ACTION_GET_CONTENT.equals(target.getAction())) return null;

        Intent prepared = withoutPersistableFlags(chooser);
        prepared.putExtra(Intent.EXTRA_INTENT, withoutPersistableFlags(target));

        Parcelable[] initial =
                IntentUtils.safeGetParcelableArrayExtra(chooser, Intent.EXTRA_INITIAL_INTENTS);
        if (initial == null) return prepared;

        Intent[] safeInitial = new Intent[initial.length];
        for (int index = 0; index < initial.length; index++) {
            if (!(initial[index] instanceof Intent candidate)
                    || !isCaptureAction(candidate.getAction())) {
                return null;
            }
            safeInitial[index] = withoutPersistableFlags(candidate);
        }
        prepared.putExtra(Intent.EXTRA_INITIAL_INTENTS, safeInitial);
        return prepared;
    }

    private static boolean isHtmlFileInputAction(@Nullable String action) {
        return Intent.ACTION_GET_CONTENT.equals(action)
                || MediaStore.ACTION_PICK_IMAGES.equals(action)
                || isCaptureAction(action);
    }

    private static boolean isCaptureAction(@Nullable String action) {
        return MediaStore.ACTION_IMAGE_CAPTURE.equals(action)
                || MediaStore.ACTION_VIDEO_CAPTURE.equals(action)
                || MediaStore.Audio.Media.RECORD_SOUND_ACTION.equals(action);
    }

    private static Intent withoutPersistableFlags(Intent source) {
        Intent copy = new Intent(source);
        copy.setFlags(source.getFlags() & ~PERSISTABLE_FLAGS);
        return copy;
    }

    static void releasePersistedResults(@Nullable Intent result, GrantReleaser releaser) {
        if (result == null) return;

        releasePersistedUri(result.getData(), releaser);
        ClipData clipData = result.getClipData();
        if (clipData == null) return;
        for (int index = 0; index < clipData.getItemCount(); index++) {
            releasePersistedUri(clipData.getItemAt(index).getUri(), releaser);
        }
    }

    private static void releasePersistedUri(@Nullable Uri uri, GrantReleaser releaser) {
        if (uri == null || !ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())) return;
        releasePersistedMode(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION, releaser);
        releasePersistedMode(uri, Intent.FLAG_GRANT_WRITE_URI_PERMISSION, releaser);
    }

    private static void releasePersistedMode(Uri uri, int modeFlags, GrantReleaser releaser) {
        try {
            releaser.release(uri, modeFlags);
        } catch (SecurityException | IllegalArgumentException ignored) {
            // GET_CONTENT normally grants nothing persistable. A rejected release is expected and
            // is not logged because the URI itself can contain sensitive provider information.
        }
    }
}
