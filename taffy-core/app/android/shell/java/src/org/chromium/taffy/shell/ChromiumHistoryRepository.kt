// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.feature.browsing.HistoryRepository
import com.taffygo.browser.ui.feature.browsing.HistorySnapshot
import com.taffygo.browser.ui.feature.browsing.HistoryVisit
import java.io.Closeable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.history.BrowsingHistoryBridge
import org.chromium.chrome.browser.history.HistoryItem
import org.chromium.chrome.browser.history.HistoryProvider
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.url.GURL

/** Window projection over Chromium's regular-profile visit store. */
class ChromiumHistoryRepository(
    private val provider: HistoryProvider,
    private val openStoredPage: suspend (String) -> Boolean,
) : HistoryRepository, HistoryProvider.BrowsingHistoryObserver, Destroyable, Closeable {

    private val state = MutableStateFlow<HistorySnapshot>(HistorySnapshot.Loading)
    private val visits = mutableListOf<HistoryVisit>()
    private val liveItems = linkedMapOf<HistoryVisit.Id, HistoryItem>()
    private var received = 0
    private var queriedPages = 0
    private var complete = true
    private var destroyed = false

    override val snapshot: StateFlow<HistorySnapshot> = state.asStateFlow()

    constructor(
        profile: Profile,
        browser: ChromiumBrowserMediator,
    ) : this(
        BrowsingHistoryBridge(profile),
        // Already on the main thread: `open` below hops before it calls this,
        // and the controller asserts that rather than hopping a second time.
        { address -> browser.taskTabs.openStoredPage(address) },
    )

    init {
        provider.setObserver(this)
        query()
    }

    override suspend fun open(id: HistoryVisit.Id): Boolean = onUiThread {
        val item = liveItems[id] ?: return@onUiThread false
        val address = exactAddress(item.url) ?: return@onUiThread false
        openStoredPage(address)
    }

    override suspend fun delete(id: HistoryVisit.Id) = onUiThread {
        val item = liveItems[id] ?: return@onUiThread
        provider.markItemForRemoval(item)
        provider.removeItems()
    }

    override fun onQueryHistoryComplete(
        items: List<HistoryItem>,
        hasMorePotentialMatches: Boolean,
    ) {
        if (destroyed) return
        queriedPages += 1
        val remaining = MAX_VISITS - received
        if (items.size > remaining) complete = false
        for (item in items) {
            if (received >= MAX_VISITS) break
            received += 1
            project(item)
        }
        if (
            hasMorePotentialMatches &&
                received < MAX_VISITS &&
                queriedPages < MAX_QUERY_PAGES
        ) {
            provider.queryHistoryContinuation()
            return
        }
        if (hasMorePotentialMatches) complete = false
        publish()
    }

    override fun onHistoryDeleted() {
        if (!destroyed) query()
    }

    override fun hasOtherFormsOfBrowsingData(hasOtherForms: Boolean) = Unit

    override fun onQueryAppsComplete(items: List<String>) = Unit

    override fun destroy() {
        if (destroyed) return
        destroyed = true
        try {
            provider.destroy()
        } finally {
            visits.clear()
            liveItems.clear()
        }
    }

    override fun close() = destroy()

    private fun query() {
        visits.clear()
        liveItems.clear()
        received = 0
        queriedPages = 0
        complete = true
        state.value = HistorySnapshot.Loading
        provider.queryHistory("", null)
    }

    private fun publish() {
        state.value = HistorySnapshot.Ready(visits.toList(), complete = complete)
    }

    /** Projects each bounded provider row once, as its continuation page arrives. */
    private fun project(item: HistoryItem) {
        if (item.wasBlockedVisit() || item.isActorVisit()) return
        val address = exactAddress(item.url) ?: return
        val host = item.url.host.takeIf(String::isNotBlank) ?: return
        val id = HistoryVisit.Id.fromStoredVisit(address, item.timestamp)
        // Chromium may repeat the boundary item between continuation pages.
        // It is the same stored visit, not a second UI row.
        if (liveItems.putIfAbsent(id, item) != null) return
        visits += HistoryVisit(
            id = id,
            title = item.title,
            host = host,
            visitedAtEpochMillis = item.timestamp,
            address = address,
        )
    }

    private fun exactAddress(url: GURL): String? = url.spec.takeIf {
        url.isValid &&
            (url.scheme == "http" || url.scheme == "https") &&
            url.username.isEmpty() &&
            url.password.isEmpty()
    }

    private suspend fun <T> onUiThread(block: suspend () -> T): T =
        withContext(Dispatchers.Main.immediate) { block() }

    private companion object {
        /** A screen is useful well below an unbounded copy of the profile database. */
        const val MAX_VISITS = 500

        /** A broken continuation source cannot keep the UI thread querying forever. */
        const val MAX_QUERY_PAGES = 16
    }
}
