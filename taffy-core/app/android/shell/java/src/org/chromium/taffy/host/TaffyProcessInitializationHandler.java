// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host;

import android.os.StrictMode;

import org.chromium.base.ContextUtils;
import org.chromium.build.BuildConfig;
import org.chromium.build.annotations.ServiceImpl;
import org.chromium.chrome.browser.download.DownloadManagerService;
import org.chromium.chrome.browser.init.ProcessInitializationHandler;

/** Browser-process owner of the regular-Dagger process root. */
@ServiceImpl(ProcessInitializationHandler.class)
public final class TaffyProcessInitializationHandler extends ProcessInitializationHandler {
    @Override
    protected void handlePreNativeLibraryLoadInitialization() {
        installTaffyDownloadNotificationOwner();
        super.handlePreNativeLibraryLoadInitialization();
        installTaffyThreadPolicy();
    }

    /** Installs the presentation sink before Chromium's process-wide download manager can exist. */
    private static void installTaffyDownloadNotificationOwner() {
        if (!DownloadManagerService.installDownloadNotifierFactory(
                TaffySuppressedUpstreamDownloadNotifier::new)) {
            throw new IllegalStateException("Download notification ownership was already claimed");
        }
    }

    /**
     * The thread half of StrictMode, which upstream installs none of.
     *
     * <p>{@code ChromeStrictMode.configureStrictMode()} — which the call to
     * {@code super} above reaches — installs a VM policy and no
     * {@link StrictMode.ThreadPolicy}, so "no blocking work on Main" had no
     * runtime detector at all. This adds one, and deliberately detects only
     * network.
     *
     * <p>Disk reads and writes are left out on purpose. The browser main thread
     * does them in upstream code this product does not own, so detecting them
     * would report a violation on nearly every startup and the product's own
     * violations would be lost in the noise — a check that always fires is a
     * check nobody reads. Network is the opposite: every Chromium network
     * request is already off the main thread by construction, so this should
     * never fire, and if it does the caller is TaffyGo's and the finding is
     * real. That is why it is worth dying for rather than logging.
     *
     * <p>Gated on {@code ENABLE_ASSERTS}, which
     * {@code enable_java_asserts = dcheck_always_on || !is_official_build}
     * makes true for the two {@code dev-*} profiles and false for
     * {@code release-arm64}, so no shipping build can be killed by it.
     */
    private static void installTaffyThreadPolicy() {
        if (!BuildConfig.ENABLE_ASSERTS) {
            return;
        }
        StrictMode.setThreadPolicy(
                new StrictMode.ThreadPolicy.Builder(StrictMode.getThreadPolicy())
                        .detectNetwork()
                        .penaltyDeath()
                        .build());
    }

    @Override
    protected void handlePostNativeInitialization() {
        super.handlePostNativeInitialization();
        ChromiumTaffyProfileRuntimeProvider.startFromBrowserProcess(
                ContextUtils.getApplicationContext());
    }
}
