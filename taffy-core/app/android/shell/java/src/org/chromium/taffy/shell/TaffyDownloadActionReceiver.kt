// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import androidx.annotation.VisibleForTesting
import java.util.concurrent.atomic.AtomicBoolean
import org.chromium.chrome.browser.init.ChromeBrowserInitializer
import org.chromium.chrome.browser.init.EmptyBrowserParts
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.taffy.browser.TaffyRegularProfileLoader
import org.chromium.taffy.host.ChromiumTaffyProfileRuntimeProvider

/** Starts Chromium, restores the exact regular profile owner, then revalidates a manual control. */
class TaffyDownloadActionReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val request = TaffyDownloadActionPayload.parse(intent) ?: return
        val pendingResult = goAsync()
        val finished = AtomicBoolean(false)
        val handler = Handler(Looper.getMainLooper())
        lateinit var timeout: Runnable
        val finish = {
            if (finished.compareAndSet(false, true)) {
                handler.removeCallbacks(timeout)
                pendingResult.finish()
            }
        }
        timeout = Runnable { finish() }
        val parts = object : EmptyBrowserParts() {
            override fun finishNativeInitialization() {
                routeDownloadControlAfterNative(
                    request = request,
                    isFinished = finished::get,
                    finish = finish,
                )
            }

            override fun onStartupFailure(failureCause: Exception?) {
                finish()
            }
        }
        try {
            handler.postDelayed(timeout, RECEIVER_TIMEOUT_MILLIS)
            val initializer = ChromeBrowserInitializer.getInstance()
            initializer.handlePreNativeStartupAndLoadLibraries(parts)
            initializer.handlePostNativeStartup(true, parts)
        } catch (_: RuntimeException) {
            finish()
        }
    }

    private companion object {
        const val RECEIVER_TIMEOUT_MILLIS = 9_000L
    }
}

/** Resolves an unloaded regular profile, restores its runtime, and revalidates one control. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun routeDownloadControlAfterNative(
    request: TaffyDownloadControlRequest,
    isFinished: () -> Boolean,
    finish: () -> Unit,
    loadProfile: (String, (Profile?) -> Unit) -> Unit = { token, callback ->
        TaffyRegularProfileLoader.load(token) { profile -> callback(profile) }
    },
    requireRuntime: (Profile) -> Unit = { profile ->
        ChromiumTaffyProfileRuntimeProvider.getStarted().requireRegularRuntime(profile)
    },
    dispatch: (TaffyDownloadControlRequest) -> Boolean =
        TaffyDownloadActionRegistry::dispatch,
) {
    if (isFinished()) return
    try {
        loadProfile(request.profileToken) { profile ->
            if (isFinished()) return@loadProfile
            try {
                if (profile != null) {
                    requireRuntime(profile)
                    dispatch(request)
                }
            } catch (_: RuntimeException) {
                // A closing profile or withdrawn native provider is a stale control, not a
                // reason for a manifest receiver to crash the browser process.
            } finally {
                finish()
            }
        }
    } catch (_: RuntimeException) {
        finish()
    }
}
