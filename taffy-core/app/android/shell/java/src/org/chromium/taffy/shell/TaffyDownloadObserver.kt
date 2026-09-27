// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.net.Uri
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import org.chromium.base.DeviceInfo
import org.chromium.base.lifetime.Destroyable
import org.chromium.components.offline_items_collection.ContentId
import org.chromium.components.offline_items_collection.LegacyHelpers
import org.chromium.components.offline_items_collection.LaunchLocation
import org.chromium.components.offline_items_collection.OfflineContentProvider
import org.chromium.components.offline_items_collection.OfflineItem
import org.chromium.components.offline_items_collection.OfflineItemState
import org.chromium.components.offline_items_collection.OpenParams
import org.chromium.components.offline_items_collection.UpdateDelta

/**
 * Projects Chromium's profile-owned download store into the small model SCR-203 consumes.
 *
 * The offline-content provider is the current Chromium seam for both historical and live
 * downloads. This observer keeps its small projection on the UI thread and retains no platform
 * item: progress callbacks can carry large artwork and metadata that the Compose list never reads.
 * It exposes only file name, source host, byte counts, and the four states TaffyGo owns.
 * Private and dangerous items never cross into Compose: private downloads belong only to their
 * private profile, while dangerous downloads remain on Chromium's warning surface until resolved.
 *
 * The initial list is asynchronous. Live changes may arrive before it, so [changesDuringInitial]
 * and [removedDuringInitial] overlay that older snapshot rather than letting it roll the screen
 * backwards. This is also why callbacks received after [destroy] are ignored.
 */
class TaffyDownloadObserver(
    private val provider: OfflineContentProvider,
    private val shareFile: (fileName: String, mimeType: String?, uri: Uri) -> Unit = { _, _, _ -> },
    private val publishReadiness: (Boolean) -> Unit = {},
    private val publishCompleteness: (Boolean) -> Unit = {},
    private val publishUnavailable: (Boolean) -> Unit = {},
    private val onSnapshotStarted: () -> Unit = {},
    private val onSnapshotSettled: () -> Unit = {},
    private val scheduleInitialTimeout: (Runnable) -> Unit = {},
    private val cancelInitialTimeout: (Runnable) -> Unit = {},
    private val publish: (List<DownloadRecord>) -> Unit,
) : OfflineContentProvider.Observer, Destroyable {

    private data class Tracked(
        val contentId: ContentId,
        val mimeType: String?,
        val createdAtEpochMillis: Long,
        val record: DownloadRecord,
    )

    // The browsing contract caps this projection at 256 rows. Keeping the bound in the index,
    // rather than taking it at the screen, prevents an old profile's complete download history
    // from living in Window memory. The ordered index makes a progress update O(log n) to file
    // and O(n) to publish, without sorting opaque ids again in Compose.
    private val current = newProjectionIndex()
    private val changesDuringInitial = newProjectionIndex()
    private val removedDuringInitial = linkedSetOf<String>()
    private var initialOverlayOverflowed = false
    private var initialGeneration = 0L
    private var initialRetries = 0
    private var awaitingInitial = false
    private var projectionComplete = false
    private var projectionUnavailable = false
    private var initialTimeout: Runnable? = null
    private var started = false
    private var destroyed = false

    /** Starts one observation and one initial profile snapshot. Idempotent. */
    fun start() {
        if (started || destroyed) return
        started = true
        awaitingInitial = true
        onSnapshotStarted()
        try {
            provider.addObserver(this)
        } catch (_: RuntimeException) {
            awaitingInitial = false
            projectionUnavailable = true
            publishCurrent()
            return
        }
        requestInitialItems()
    }

    /** Rechecks the current provider projection before performing a manual action. */
    fun perform(id: DownloadId, action: DownloadAction): Boolean {
        if (destroyed) return false
        val tracked = current[id.value] ?: return false
        if (action !in tracked.record.allowedActions) return false
        when (action) {
            DownloadAction.PAUSE -> provider.pauseDownload(tracked.contentId.copy())
            DownloadAction.RESUME -> provider.resumeDownload(tracked.contentId.copy())
            DownloadAction.CANCEL -> provider.cancelDownload(tracked.contentId.copy())
            DownloadAction.OPEN -> provider.openItem(
                OpenParams(LaunchLocation.DOWNLOAD_HOME),
                tracked.contentId.copy(),
            )
            DownloadAction.SHARE -> requestShare(tracked)
            DownloadAction.REMOVE -> provider.removeItem(tracked.contentId.copy())
        }
        return true
    }

    override fun onItemsAdded(items: List<OfflineItem>) {
        if (destroyed) return
        var publicStateChanged = false
        for (item in items) {
            // Call upsert even after one change: `any` would stop at the first
            // true result and silently drop the rest of a provider batch.
            publicStateChanged = upsert(item) || publicStateChanged
        }
        if (publicStateChanged) publishCurrent()
    }

    override fun onItemUpdated(item: OfflineItem, updateDelta: UpdateDelta) {
        if (destroyed) return
        if (upsert(item)) publishCurrent()
    }

    override fun onItemRemoved(id: ContentId) {
        if (destroyed) return
        val token = tokenFor(id) ?: return
        val visibleChanged = current.remove(token)
        if (awaitingInitial) {
            changesDuringInitial.remove(token)
            rememberInitialRemoval(token)
        }
        if (visibleChanged) publishCurrent()
        if (!awaitingInitial && !projectionComplete) {
            // A bounded projection cannot know which older item should refill
            // a removed row, or whether deleting an unseen old row made the
            // profile complete. Re-read once and let the existing bounded
            // initial overlay absorb any callbacks that race that answer.
            awaitingInitial = true
            initialRetries = 0
            clearInitialOverlay()
            rememberInitialRemoval(token)
            onSnapshotStarted()
            requestInitialItems()
        }
    }

    override fun destroy() {
        if (destroyed) return
        destroyed = true
        try {
            if (started) provider.removeObserver(this)
        } finally {
            started = false
            current.clear()
            projectionComplete = false
            projectionUnavailable = false
            initialGeneration++
            cancelPendingInitialTimeout()
            clearInitialOverlay()
        }
    }

    private fun requestInitialItems() {
        val generation = ++initialGeneration
        cancelPendingInitialTimeout()
        val timeout = Runnable {
            if (destroyed || !awaitingInitial || generation != initialGeneration) return@Runnable
            initialTimeout = null
            projectionUnavailable = true
            publishCurrent()
        }
        initialTimeout = timeout
        scheduleInitialTimeout(timeout)
        try {
            provider.getAllItems { items -> onInitialItems(generation, items) }
        } catch (_: RuntimeException) {
            if (destroyed || generation != initialGeneration) return
            cancelPendingInitialTimeout()
            awaitingInitial = false
            initialRetries = 0
            projectionUnavailable = true
            clearInitialOverlay()
            publishCurrent()
        }
    }

    private fun onInitialItems(generation: Long, items: ArrayList<OfflineItem>) {
        if (destroyed || !awaitingInitial || generation != initialGeneration) return
        cancelPendingInitialTimeout()
        if (initialOverlayOverflowed) {
            // A removal callback has no chronology, so dropping old tombstones
            // could resurrect a row from this now-stale snapshot. Discard it
            // and ask once more from an empty, bounded overlay instead.
            if (initialRetries >= MAX_INITIAL_RETRIES) {
                awaitingInitial = false
                initialRetries = 0
                clearInitialOverlay()
                publishCurrent()
            } else {
                initialRetries++
                clearInitialOverlay()
                requestInitialItems()
            }
            return
        }
        val snapshot = newProjectionIndex()
        for (item in items) {
            val token = tokenFor(item.id) ?: continue
            if (token in removedDuringInitial) continue
            val tracked = project(item, current[token]) ?: continue
            snapshot.upsert(tracked)
        }
        changesDuringInitial.values().forEach { snapshot.upsert(it) }
        removedDuringInitial.forEach { snapshot.remove(it) }

        projectionComplete = !snapshot.overflowed && !changesDuringInitial.overflowed
        projectionUnavailable = false
        current.replaceWith(snapshot)
        clearInitialOverlay()
        awaitingInitial = false
        initialRetries = 0
        publishCurrent()
        onSnapshotSettled()
    }

    private fun rememberInitialRemoval(token: String) {
        if (token in removedDuringInitial || initialOverlayOverflowed) return
        if (removedDuringInitial.size >= MAX_INITIAL_REMOVALS) {
            initialOverlayOverflowed = true
            return
        }
        removedDuringInitial += token
    }

    private fun clearInitialOverlay() {
        changesDuringInitial.clear()
        removedDuringInitial.clear()
        initialOverlayOverflowed = false
    }

    private fun cancelPendingInitialTimeout() {
        initialTimeout?.let(cancelInitialTimeout)
        initialTimeout = null
    }

    /**
     * Files one provider item and reports whether public rows or completeness changed.
     *
     * Chromium's update delta includes fields this projection deliberately does
     * not expose. Retaining those callbacks is still important while the
     * initial snapshot is in flight, but republishing an equal [DownloadRecord]
     * only invalidates the screen and redraws every visible row for no result.
     */
    private fun upsert(item: OfflineItem): Boolean {
        val wasComplete = projectionComplete
        val token = tokenFor(item.id) ?: return false
        val previous = current[token]
        val tracked = project(item, previous)
        if (tracked == null) {
            val visibleChanged = current.remove(token)
            if (awaitingInitial) {
                changesDuringInitial.remove(token)
                rememberInitialRemoval(token)
            }
            return visibleChanged
        }
        val visibleChanged = current.upsert(tracked)
        if (!awaitingInitial && current.overflowed) projectionComplete = false
        if (previous !== tracked) {
            if (awaitingInitial) {
                changesDuringInitial.upsert(tracked)
                removedDuringInitial.remove(token)
            }
        }
        return visibleChanged || wasComplete != projectionComplete
    }

    private fun publishCurrent() {
        publish(current.values().map(Tracked::record))
        publishCompleteness(projectionComplete)
        publishUnavailable(projectionUnavailable)
        publishReadiness(!awaitingInitial || projectionUnavailable)
    }

    private fun requestShare(requested: Tracked) {
        val token = requested.record.id.value
        provider.getShareInfoForItem(requested.contentId.copy()) { returnedId, info ->
            if (destroyed || tokenFor(returnedId) != token) return@getShareInfoForItem
            val currentItem = current[token] ?: return@getShareInfoForItem
            if (DownloadAction.SHARE !in currentItem.record.allowedActions) {
                return@getShareInfoForItem
            }
            val uri = info?.uri ?: return@getShareInfoForItem
            if (!uri.scheme.equals("content", ignoreCase = true)) return@getShareInfoForItem
            shareFile(currentItem.record.fileName, currentItem.mimeType, uri)
        }
    }

    private fun project(item: OfflineItem, previous: Tracked?): Tracked? {
        val id = item.id ?: return null
        val token = tokenFor(id) ?: return null
        if (!isDownload(id) || item.isOffTheRecord || item.isDangerous) return null

        val state = stateFor(item) ?: return null
        val fileName = item.title.takeIf(String::isNotBlank)
            ?.let { boundedDownloadText(it, MAX_FILE_NAME_BYTES) }
            ?.takeIf(String::isNotBlank)
            ?: item.filePath?.substringAfterLast('/')?.takeIf(String::isNotBlank)
                ?.let { boundedDownloadText(it, MAX_FILE_NAME_BYTES) }
                ?.takeIf(String::isNotBlank)
            ?: previous?.record?.fileName
            ?: return null
        val host = item.url?.host?.takeIf(String::isNotBlank)
            ?.let { boundedDownloadText(it, MAX_HOST_BYTES) }
            ?.takeIf(String::isNotBlank)
            ?: previous?.record?.host.orEmpty()
        val total = item.totalSizeBytes.takeIf { it >= 0 } ?: previous?.record?.totalBytes
        var received = item.receivedBytes.takeIf { it >= 0 }
            ?: previous?.record?.downloadedBytes
            ?: 0L
        if (total != null) received = received.coerceAtMost(total)

        val mimeType = item.mimeType?.takeIf {
            it.isNotBlank() && downloadTextFits(it, MAX_MIME_TYPE_BYTES)
        } ?: previous?.mimeType
        val createdAtEpochMillis = item.creationTimeMs.takeIf { it > 0 }
            ?: previous?.createdAtEpochMillis
            ?: 0
        val allowedActions = actionsFor(item, state)
        val previousRecord = previous?.record
        val record = previousRecord?.takeIf { held ->
            held.fileName == fileName &&
                held.host == host &&
                held.totalBytes == total &&
                held.downloadedBytes == received.coerceAtLeast(0) &&
                held.state == state &&
                held.mimeType == mimeType &&
                held.allowedActions == allowedActions
        } ?: DownloadRecord(
            id = DownloadId(token),
            fileName = fileName,
            host = host,
            totalBytes = total,
            downloadedBytes = received.coerceAtLeast(0),
            state = state,
            allowedActions = allowedActions,
            mimeType = mimeType,
        )
        if (
            previous != null && record === previousRecord && mimeType == previous.mimeType &&
                createdAtEpochMillis == previous.createdAtEpochMillis
        ) {
            return previous
        }
        return Tracked(
            contentId = previous?.contentId ?: id.copy(),
            mimeType = mimeType,
            createdAtEpochMillis = createdAtEpochMillis,
            record = record,
        )
    }

    /** Returns one canonical immutable set; a progress tick allocates no action collection. */
    private fun actionsFor(item: OfflineItem, state: DownloadState): Set<DownloadAction> {
        val mayRemove = !item.isTransient
        return when (state) {
            DownloadState.RUNNING -> when {
                item.isResumable -> PAUSE_CANCEL_ACTIONS
                else -> CANCEL_ACTIONS
            }
            DownloadState.PAUSED -> when {
                item.isResumable -> RESUME_CANCEL_ACTIONS
                else -> CANCEL_ACTIONS
            }
            DownloadState.COMPLETE -> {
                val mayOpen = item.isOpenable && !item.externallyRemoved
                val mayShare = mayOpen && LegacyHelpers.isLegacyDownload(item.id) &&
                    !DeviceInfo.isAutomotive()
                when {
                    mayShare && mayRemove -> OPEN_SHARE_REMOVE_ACTIONS
                    mayShare -> OPEN_SHARE_ACTIONS
                    mayOpen && mayRemove -> OPEN_REMOVE_ACTIONS
                    mayOpen -> OPEN_ACTIONS
                    mayRemove -> REMOVE_ACTIONS
                    else -> NO_ACTIONS
                }
            }
            DownloadState.FAILED -> when {
                item.isResumable && mayRemove -> RESUME_REMOVE_ACTIONS
                item.isResumable -> RESUME_ACTIONS
                mayRemove -> REMOVE_ACTIONS
                else -> NO_ACTIONS
            }
        }
    }

    private fun stateFor(item: OfflineItem): DownloadState? {
        if (item.externallyRemoved) return DownloadState.FAILED
        return when (item.state) {
            OfflineItemState.PENDING,
            OfflineItemState.IN_PROGRESS,
            -> DownloadState.RUNNING

            OfflineItemState.PAUSED -> DownloadState.PAUSED
            OfflineItemState.COMPLETE -> DownloadState.COMPLETE
            OfflineItemState.CANCELLED,
            OfflineItemState.INTERRUPTED,
            OfflineItemState.FAILED,
            -> DownloadState.FAILED

            else -> null
        }
    }

    private fun isDownload(id: ContentId): Boolean =
        LegacyHelpers.isLegacyDownload(id) || LegacyHelpers.isLegacyAndroidDownload(id)

    private fun tokenFor(id: ContentId?): String? {
        val namespace = id?.namespace?.takeIf(String::isNotBlank) ?: return null
        val value = id.id?.takeIf(String::isNotBlank) ?: return null
        val token = "${namespace.length}:$namespace$value"
        return token.takeIf { downloadTextFits(it, MAX_IDENTIFIER_BYTES) }
    }

    private fun ContentId.copy(): ContentId = ContentId(namespace, id)

    private fun newProjectionIndex() = BoundedNewestFirstIndex(
        maxSize = MAX_VISIBLE_DOWNLOADS,
        tokenOf = { tracked: Tracked -> tracked.record.id.value },
        timestampOf = Tracked::createdAtEpochMillis,
        sameVisibleValue = { previous, next -> previous.record == next.record },
    )

    private companion object {
        // Machine owner: contracts/browsing/schema/contract.json MAX_DOWNLOADS.
        const val MAX_VISIBLE_DOWNLOADS = 256
        const val MAX_IDENTIFIER_BYTES = 256
        const val MAX_HOST_BYTES = 253
        const val MAX_FILE_NAME_BYTES = 512
        const val MAX_MIME_TYPE_BYTES = 256
        const val MAX_INITIAL_REMOVALS = MAX_VISIBLE_DOWNLOADS
        const val MAX_INITIAL_RETRIES = 2
        val NO_ACTIONS: Set<DownloadAction> = emptySet()
        val REMOVE_ACTIONS: Set<DownloadAction> = setOf(DownloadAction.REMOVE)
        val CANCEL_ACTIONS: Set<DownloadAction> = setOf(DownloadAction.CANCEL)
        val PAUSE_CANCEL_ACTIONS: Set<DownloadAction> =
            setOf(DownloadAction.PAUSE, DownloadAction.CANCEL)
        val RESUME_ACTIONS: Set<DownloadAction> = setOf(DownloadAction.RESUME)
        val RESUME_CANCEL_ACTIONS: Set<DownloadAction> =
            setOf(DownloadAction.RESUME, DownloadAction.CANCEL)
        val RESUME_REMOVE_ACTIONS: Set<DownloadAction> =
            setOf(DownloadAction.RESUME, DownloadAction.REMOVE)
        val OPEN_ACTIONS: Set<DownloadAction> = setOf(DownloadAction.OPEN)
        val OPEN_REMOVE_ACTIONS: Set<DownloadAction> =
            setOf(DownloadAction.OPEN, DownloadAction.REMOVE)
        val OPEN_SHARE_ACTIONS: Set<DownloadAction> =
            setOf(DownloadAction.OPEN, DownloadAction.SHARE)
        val OPEN_SHARE_REMOVE_ACTIONS: Set<DownloadAction> =
            setOf(DownloadAction.OPEN, DownloadAction.SHARE, DownloadAction.REMOVE)
    }

}
