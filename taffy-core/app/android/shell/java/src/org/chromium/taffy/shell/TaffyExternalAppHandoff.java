// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.external_intents.ExternalNavigationParams;
import org.chromium.url.GURL;

import java.net.URISyntaxException;
import java.util.Locale;
import java.util.Set;

/** Sanitizes one manual external-protocol navigation and opens Android's chooser. */
final class TaffyExternalAppHandoff {
    private static final int MAX_ADDRESS_LENGTH = 8_192;
    private static final Set<String> BROWSER_ONLY_SCHEMES =
            Set.of(
                    "about",
                    "android-app",
                    "blob",
                    "chrome",
                    "chrome-native",
                    "content",
                    "data",
                    "devtools",
                    "file",
                    "filesystem",
                    "ftp",
                    "http",
                    "https",
                    "javascript",
                    "view-source");

    interface ChooserLauncher {
        boolean launch(Intent sanitizedTarget);
    }

    private final TaffyManualGestureGate mGestureGate;
    private final ChooserLauncher mLauncher;
    private boolean mClosed;

    TaffyExternalAppHandoff(Activity activity, TaffyManualGestureGate gestureGate) {
        this(gestureGate, target -> launchChooser(activity, target));
    }

    @VisibleForTesting
    TaffyExternalAppHandoff(TaffyManualGestureGate gestureGate, ChooserLauncher chooserLauncher) {
        mGestureGate = gestureGate;
        mLauncher = chooserLauncher;
    }

    /** Returns true only after the system chooser was started for a sanitized external target. */
    boolean open(Tab tab, @Nullable ExternalNavigationParams params) {
        if (mClosed || !isEligible(tab, params)) return false;
        GURL address = params.getUrl();
        if (!isExternalCandidate(address)) return false;
        if (!mGestureGate.consume(tab.getId())) return false;
        Intent target = sanitizedTarget(address);
        if (target == null) return false;
        return mLauncher.launch(target);
    }

    void close() {
        mClosed = true;
    }

    @VisibleForTesting
    static @Nullable Intent sanitizedTarget(GURL address) {
        String spec = address.getSpec();
        if (!address.isValid() || spec.isEmpty() || spec.length() > MAX_ADDRESS_LENGTH) return null;
        Uri data;
        try {
            data =
                    "intent".equals(address.getScheme())
                            ? Intent.parseUri(spec, Intent.URI_INTENT_SCHEME).getData()
                            : Uri.parse(spec);
        } catch (URISyntaxException invalidIntent) {
            return null;
        }
        if (data == null || data.getScheme() == null) return null;
        String scheme = data.getScheme().toLowerCase(Locale.ROOT);
        if (!isExternalScheme(scheme)) return null;

        // Rebuilding from data is deliberate: package, component, selector, fallback address,
        // extras, clip data and grant/task flags encoded by an intent URI never cross the seam.
        return new Intent(Intent.ACTION_VIEW, data).addCategory(Intent.CATEGORY_BROWSABLE);
    }

    private static boolean isEligible(Tab tab, @Nullable ExternalNavigationParams params) {
        return params != null
                && !tab.isDestroyed()
                && !tab.isClosing()
                && !tab.isOffTheRecord()
                && !params.isIncognito()
                && params.isMainFrame()
                && params.isRendererInitiated()
                && params.hasUserGesture()
                && !params.isBackgroundTabNavigation()
                && !params.isHiddenCrossFrameNavigation()
                && !params.isSandboxedMainFrame()
                && !params.isFromIntent();
    }

    private static boolean isExternalScheme(String scheme) {
        if (BROWSER_ONLY_SCHEMES.contains(scheme) || "intent".equals(scheme)) return false;
        if (scheme.isEmpty() || scheme.charAt(0) < 'a' || scheme.charAt(0) > 'z') return false;
        for (int index = 1; index < scheme.length(); index++) {
            char character = scheme.charAt(index);
            if (!(character >= 'a' && character <= 'z')
                    && !(character >= '0' && character <= '9')
                    && character != '+'
                    && character != '-'
                    && character != '.') {
                return false;
            }
        }
        return true;
    }

    private static boolean isExternalCandidate(GURL address) {
        String spec = address.getSpec();
        if (!address.isValid() || spec.isEmpty() || spec.length() > MAX_ADDRESS_LENGTH)
            return false;
        String scheme = address.getScheme().toLowerCase(Locale.ROOT);
        return "intent".equals(scheme) || isExternalScheme(scheme);
    }

    private static boolean launchChooser(Activity activity, Intent target) {
        if (activity.isFinishing() || activity.isDestroyed()) {
            return false;
        }
        try {
            activity.startActivity(Intent.createChooser(target, /* title= */ null));
            return true;
        } catch (ActivityNotFoundException | SecurityException unavailable) {
            return false;
        }
    }
}
