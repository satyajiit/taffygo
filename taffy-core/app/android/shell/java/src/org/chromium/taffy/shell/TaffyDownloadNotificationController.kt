// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadRecord
import java.io.Closeable
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.components.offline_items_collection.OfflineContentProvider
import org.chromium.taffy.browser.TaffyDownloadProviderBridge
import org.chromium.taffy.shell.TaffyDownloadNotificationProjection.Effect

/** One regular profile's observer, bounded notification projection, and manual controls. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class TaffyDownloadNotificationController(
    provider: OfflineContentProvider?,
    private val profileToken: String,
    private val sink: TaffyDownloadNotificationSink,
    scheduleInitialTimeout: (Runnable) -> Unit = {},
    cancelInitialTimeout: (Runnable) -> Unit = {},
) : Closeable {
    private val projection = TaffyDownloadNotificationProjection()
    private val pendingControls = ArrayDeque<TaffyDownloadControlRequest>()
    private val registryRegistration: Closeable =
        TaffyDownloadActionRegistry.register(profileToken, ::perform)
    private val observer = provider?.let { source ->
        TaffyDownloadObserver(
            provider = source,
            publishReadiness = ::onReadiness,
            publishCompleteness = ::onCompleteness,
            onSnapshotStarted = projection::beginSnapshot,
            onSnapshotSettled = ::onSnapshotSettled,
            scheduleInitialTimeout = scheduleInitialTimeout,
            cancelInitialTimeout = cancelInitialTimeout,
            publish = ::onRecords,
        )
    }
    private var ready = provider == null
    private var complete = false
    private var latestRecords = emptyList<DownloadRecord>()
    private var destroyed = false

    init {
        observer?.start()
    }

    override fun close() {
        if (destroyed) return
        destroyed = true
        registryRegistration.close()
        pendingControls.clear()
        observer?.destroy()
        latestRecords = emptyList()
        projection.close().forEach(::apply)
        sink.cancelProfile(profileToken)
    }

    private fun onRecords(records: List<DownloadRecord>) {
        if (destroyed) return
        latestRecords = records
        projection.accept(records).forEach(::apply)
    }

    private fun onReadiness(isReady: Boolean) {
        if (destroyed || !isReady || ready) return
        ready = true
        while (pendingControls.isNotEmpty()) {
            performNow(pendingControls.removeFirst())
        }
    }

    private fun onCompleteness(isComplete: Boolean) {
        if (!destroyed) complete = isComplete
    }

    private fun onSnapshotSettled() {
        if (destroyed) return
        projection.settleSnapshot()
        if (complete) sink.reconcile(profileToken, latestRecords.mapTo(linkedSetOf()) { it.id })
    }

    private fun perform(request: TaffyDownloadControlRequest): Boolean {
        if (destroyed || request.profileToken != profileToken || !isControl(request.action)) {
            return false
        }
        if (ready) return performNow(request)
        if (pendingControls.any { it == request }) return true
        if (pendingControls.size >= MAX_PENDING_CONTROLS) return false
        pendingControls.addLast(request)
        return true
    }

    private fun performNow(request: TaffyDownloadControlRequest): Boolean = try {
        observer?.perform(request.downloadId, request.action) ?: false
    } catch (_: RuntimeException) {
        // Native ownership may disappear between the current-snapshot check and the control call.
        false
    }

    private fun apply(effect: Effect) {
        when (effect) {
            is Effect.Cancel -> sink.cancel(profileToken, effect.id)
            is Effect.Show -> {
                if (!sink.show(profileToken, effect.record)) {
                    projection.retryOnNextSnapshot(effect.record.id)
                }
            }
        }
    }

    private fun isControl(action: DownloadAction): Boolean = when (action) {
        DownloadAction.PAUSE,
        DownloadAction.RESUME,
        DownloadAction.CANCEL,
        -> true

        DownloadAction.OPEN,
        DownloadAction.SHARE,
        DownloadAction.REMOVE,
        -> false
    }

    companion object {
        private const val INITIAL_TIMEOUT_MILLIS = 10_000L
        private const val MAX_PENDING_CONTROLS = 32

        fun open(
            context: Context,
            profile: Profile,
            profileToken: String,
        ): TaffyDownloadNotificationController {
            check(!profile.isOffTheRecord) { "Private profiles cannot own download notifications" }
            val handler = Handler(Looper.getMainLooper())
            return TaffyDownloadNotificationController(
                provider = TaffyDownloadProviderBridge.forProfile(profile),
                profileToken = profileToken,
                sink = TaffyDownloadNotifier(context),
                scheduleInitialTimeout = { task ->
                    handler.postDelayed(task, INITIAL_TIMEOUT_MILLIS)
                },
                cancelInitialTimeout = handler::removeCallbacks,
            )
        }
    }
}
