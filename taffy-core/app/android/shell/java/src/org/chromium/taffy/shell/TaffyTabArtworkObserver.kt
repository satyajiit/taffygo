// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.graphics.Bitmap
import android.util.Size
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.TabId
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tab.TabFavicon
import org.chromium.chrome.browser.tab_ui.TabContentManager
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabModelObserver
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver
import org.chromium.url.GURL

/**
 * Favicons and page snapshots for the tab switcher, from the local engine.
 *
 * Chrome's own cache, not a network fetch: [TabFavicon.getBitmap] is what the
 * engine already stored for the page, and [TabContentManager] is the snapshot
 * cache that was previously left off because the switcher drew a named
 * placeholder.
 * A missing bitmap is a silent slab on the card — this observer never invents
 * a caption for it.
 */
class TaffyTabArtworkObserver(
    private val selector: TabModelSelector,
    private val tabContentManager: TabContentManager,
    private val publish: (Map<TabId, TabArtwork>) -> Unit,
) : Destroyable {

    private val artwork = mutableMapOf<TabId, TabArtwork>()
    private val liveTabs = mutableMapOf<TabId, Tab>()
    private val thumbnailRequests = mutableMapOf<TabId, Any>()
    private val thumbnailSize = Size(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT)
    private var suppressPublications = false
    private var publicationPending = false
    private var destroyed = false

    private val tabs = object : TabModelSelectorTabObserver(selector) {
        override fun onTabRegistered(tab: Tab) {
            capture(tab)
            requestThumbnail(tab)
        }

        override fun onTabUnregistered(tab: Tab) {
            drop(tab)
        }

        override fun onShown(tab: Tab, type: Int) {
            capture(tab)
            requestThumbnail(tab)
        }

        override fun didFirstVisuallyNonEmptyPaint(tab: Tab) {
            if (destroyed || liveTabs[idOf(tab)] !== tab || tab !== selector.currentTab ||
                tab.isOffTheRecord || tab.isDestroyed || tab.isHidden
            ) return
            val url = tab.url
            if (!url.isValid || (url.scheme != "http" && url.scheme != "https")) return
            // Compose parks the selected page when opening the overview; that never calls
            // onHidden. First paint supplies real content and does not repeat for typing.
            // These are local browser cards; Chromium's read waits for the JPEG write.
            tabContentManager.cacheTabThumbnail(tab)
            requestThumbnail(tab)
        }

        override fun onHidden(tab: Tab, type: Int) {
            tabContentManager.cacheTabThumbnail(tab)
            requestThumbnail(tab)
        }

        override fun onFaviconUpdated(tab: Tab, icon: Bitmap?, iconUrl: GURL?) {
            put(
                tab,
                favicon = icon ?: TabFavicon.getBitmap(tab),
                thumbnail = artwork[idOf(tab)]?.thumbnail,
            )
        }

        override fun onUrlUpdated(tab: Tab) {
            capture(tab)
        }
    }

    private val models = object : TabModelSelectorTabModelObserver(selector) {
        override fun didRemoveTabForClosure(tab: Tab) = drop(tab)

        override fun tabRemoved(tab: Tab) = drop(tab)

        override fun restoreCompleted() = refreshAll()
    }

    init {
        refreshAll()
    }

    override fun destroy() {
        if (destroyed) return
        destroyed = true
        tabs.destroy()
        models.destroy()
        thumbnailRequests.clear()
        liveTabs.clear()
        artwork.clear()
        publicationPending = false
    }

    private fun refreshAll() {
        if (destroyed) return
        suppressPublications = true
        val live = mutableSetOf<TabId>()
        try {
            selector.models.forEach { model ->
                for (index in 0 until model.count) {
                    val tab = model.getTabAt(index) ?: continue
                    val id = idOf(tab)
                    live += id
                    liveTabs[id] = tab
                    capture(tab)
                    requestThumbnail(tab)
                }
            }
            val stale = liveTabs.keys.filterNot { it in live }
            stale.forEach { id ->
                liveTabs.remove(id)
                thumbnailRequests.remove(id)
                if (artwork.remove(id) != null) publicationPending = true
            }
        } finally {
            suppressPublications = false
            publishIfNeeded()
        }
    }

    private fun capture(tab: Tab) {
        if (destroyed) return
        val id = idOf(tab)
        liveTabs[id] = tab
        // `TabFavicon.getBitmap` is empty until the engine's own favicon
        // helper has been constructed for this tab — which this activity
        // never does. An earlier `onFaviconUpdated` (or a mark we already
        // hold) is the real answer; writing null over it is how the switcher
        // lost every mark the moment the URL ticked.
        val stored = TabFavicon.getBitmap(tab)
        val held = artwork[id]?.favicon
        put(tab, favicon = stored ?: held, thumbnail = artwork[id]?.thumbnail)
    }

    private fun requestThumbnail(tab: Tab) {
        if (destroyed) return
        val id = idOf(tab)
        if (liveTabs[id] !== tab) return
        val request = Any()
        thumbnailRequests[id] = request
        tabContentManager.getTabThumbnailWithCallback(tab.id, thumbnailSize) { bitmap ->
            if (destroyed || liveTabs[id] !== tab || thumbnailRequests[id] !== request) {
                return@getTabThumbnailWithCallback
            }
            thumbnailRequests.remove(id)
            val current = artwork[id]
            put(
                tab,
                favicon = current?.favicon ?: TabFavicon.getBitmap(tab),
                thumbnail = bitmap,
            )
        }
    }

    private fun put(tab: Tab, favicon: Bitmap?, thumbnail: Bitmap?) {
        val id = idOf(tab)
        if (destroyed || liveTabs[id] !== tab) return
        val next = TabArtwork(favicon = favicon, thumbnail = thumbnail)
        val previous = artwork[id]
        if (previous != null &&
            previous.favicon === next.favicon &&
            previous.thumbnail === next.thumbnail
        ) {
            return
        }
        artwork[id] = next
        publicationPending = true
        publishIfNeeded()
    }

    private fun drop(tab: Tab) {
        if (destroyed) return
        val id = idOf(tab)
        if (liveTabs[id] !== tab) return
        liveTabs.remove(id)
        thumbnailRequests.remove(id)
        if (artwork.remove(id) == null) return
        publicationPending = true
        publishIfNeeded()
    }

    private fun publishIfNeeded() {
        if (destroyed || suppressPublications || !publicationPending) return
        publicationPending = false
        publish(artwork.toMap())
    }

    private fun idOf(tab: Tab): TabId = TabId(tab.id.toString())

    private companion object {
        const val THUMBNAIL_WIDTH: Int = 360
        const val THUMBNAIL_HEIGHT: Int = 200
    }
}
