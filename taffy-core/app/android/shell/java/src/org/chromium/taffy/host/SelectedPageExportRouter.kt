// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.page.PageIntelligenceClient
import kotlinx.coroutines.CancellationException
import taffy.core_api.CoreApiSubmissionStatus
import taffy.core_api.PageSnapshotExportAvailability
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.PageSnapshotExportResult

/** Keeps one export bound to the tab client that accepted it. */
class SelectedPageExportRouter(
    private val selectedClient: () -> PageIntelligenceClient?,
) {
    private data class Route(
        val client: PageIntelligenceClient,
        val selectionGeneration: Long,
    )

    private val lock = Any()
    private val routes = mutableMapOf<String, Route>()
    private var selectionGeneration = 0L
    private var closed = false

    fun selectionChanged() {
        synchronized(lock) {
            if (!closed) selectionGeneration += 1
        }
    }

    suspend fun exportSnapshot(
        requestId: String,
        documentId: String,
        format: PageSnapshotExportFormat,
    ): PageSnapshotExportResult {
        val route = synchronized(lock) {
            if (closed) return noSelectedPage()
            if (routes.containsKey(requestId)) return replayConflict()
            val client = selectedClient() ?: return noSelectedPage()
            Route(client, selectionGeneration).also { routes[requestId] = it }
        }
        val result = try {
            route.client.exportSnapshot(requestId, documentId, format)
        } catch (cancelled: CancellationException) {
            removeExact(requestId, route)
            throw cancelled
        } catch (failure: Throwable) {
            removeExact(requestId, route)
            throw failure
        }
        val completion = synchronized(lock) {
            val exact = routes[requestId] === route
            if (exact) routes.remove(requestId)
            exact to (exact && !closed && route.selectionGeneration == selectionGeneration)
        }
        if (completion.second) return result
        if (completion.first) {
            route.client.cancelExport(requestId)
        }
        return PageSnapshotExportResult(PageSnapshotExportAvailability.STALE_DOCUMENT, null)
    }

    suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus {
        val client = synchronized(lock) {
            if (closed) return CoreApiSubmissionStatus.INVALID_REQUEST
            routes.remove(requestId)?.client
        } ?: return CoreApiSubmissionStatus.INVALID_REQUEST
        return client.cancelExport(requestId)
    }

    fun close() {
        synchronized(lock) {
            if (closed) return
            closed = true
            selectionGeneration += 1
            routes.clear()
        }
    }

    private fun removeExact(requestId: String, route: Route) {
        synchronized(lock) {
            if (routes[requestId] === route) routes.remove(requestId)
        }
    }

    private fun noSelectedPage() = PageSnapshotExportResult(
        PageSnapshotExportAvailability.NO_SELECTED_PAGE,
        null,
    )

    private fun replayConflict() = PageSnapshotExportResult(
        PageSnapshotExportAvailability.REPLAY_CONFLICT,
        null,
    )
}
