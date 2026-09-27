// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.Context
import android.os.Handler
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.taffy.browser.TaffyDownloadProviderBridge

/** Owns the exact-profile Chromium download adapter for one product window. */
internal class ChromiumDownloadController(
    context: Context,
    profile: Profile,
    mainHandler: Handler,
) : Destroyable {
    private val downloadsState = MutableStateFlow(emptyList<DownloadRecord>())
    private val readyState = MutableStateFlow(false)
    private val completeState = MutableStateFlow(false)
    private val unavailableState = MutableStateFlow(false)
    private val share = TaffyDownloadShare(context)
    private val pdf = TaffyTaskPdfHandoff(context)
    private val provider = TaffyDownloadProviderBridge.forProfile(profile)
    private val observer = provider?.let { provider ->
        TaffyDownloadObserver(
            provider = provider,
            shareFile = share::showChooser,
            publishReadiness = { readyState.value = it },
            publishCompleteness = { completeState.value = it },
            publishUnavailable = { unavailableState.value = it },
            scheduleInitialTimeout = { task ->
                mainHandler.postDelayed(task, INITIAL_TIMEOUT_MILLIS)
            },
            cancelInitialTimeout = mainHandler::removeCallbacks,
            publish = { downloadsState.value = it },
        )
    }
    private val taskActions = TaskDownloadActions(
        current = { downloadsState.value },
        canOpen = { taskId, guid ->
            TaffyDownloadProviderBridge.canOpenTaskDownload(profile, taskId, guid)
        },
        perform = ::perform,
        requestFile = { id, callback ->
            if (provider == null) callback.onShareInfoAvailable(id, null)
            else provider.getShareInfoForItem(id, callback)
        },
        openPdf = pdf::open,
    )

    val downloads: StateFlow<List<DownloadRecord>> = downloadsState.asStateFlow()
    val ready: StateFlow<Boolean> = readyState.asStateFlow()
    val complete: StateFlow<Boolean> = completeState.asStateFlow()
    val unavailable: StateFlow<Boolean> = unavailableState.asStateFlow()

    init {
        if (observer == null) {
            unavailableState.value = true
            readyState.value = true
        } else {
            observer.start()
        }
    }

    fun perform(id: DownloadId, action: DownloadAction): Boolean =
        observer?.perform(id, action) ?: false

    fun completedForTask(taskId: String): List<DownloadRecord> =
        taskActions.completedForTask(taskId)

    suspend fun openForTask(taskId: String, id: DownloadId): Boolean =
        taskActions.openForTask(taskId, id)

    override fun destroy() {
        taskActions.destroy()
        observer?.destroy()
        downloadsState.value = emptyList()
    }

    private companion object {
        const val INITIAL_TIMEOUT_MILLIS = 10_000L
    }
}
