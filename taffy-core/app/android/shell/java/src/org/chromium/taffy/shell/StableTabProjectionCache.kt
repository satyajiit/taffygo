// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.model.Tab as TaffyTab
import com.taffygo.browser.ui.core.model.TabId as TaffyTabId
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tabmodel.TabModel

/** Reuses unchanged tab projections while preserving the selector's current order. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class StableTabProjectionCache<T : Any>(
    private val idOf: (T) -> Int,
    private val project: (T, isSelected: Boolean) -> TaffyTab,
) {
    private val byId = mutableMapOf<Int, TaffyTab>()
    private val dirtyIds = mutableSetOf<Int>()
    private var invalidateEveryTab = true
    private var selectedId: Int? = null
    private var published = emptyList<TaffyTab>()

    fun invalidate(id: Int) {
        dirtyIds += id
    }

    fun invalidateAll() {
        invalidateEveryTab = true
        dirtyIds.clear()
    }

    /** Reads expensive fields only for new, dirty, or selection-changing tabs. */
    fun snapshot(tabs: Iterable<T>, nextSelectedId: Int?): List<TaffyTab> {
        val previousSelectedId = selectedId
        val selectionChanged = previousSelectedId != nextSelectedId
        val liveIds = hashSetOf<Int>()
        val next = mutableListOf<TaffyTab>()
        for (tab in tabs) {
            val id = idOf(tab)
            if (!liveIds.add(id)) continue
            val selectionAffected = selectionChanged &&
                (id == previousSelectedId || id == nextSelectedId)
            val projected = if (
                invalidateEveryTab || id in dirtyIds || id !in byId || selectionAffected
            ) {
                project(tab, id == nextSelectedId).also { byId[id] = it }
            } else {
                checkNotNull(byId[id])
            }
            next += projected
        }

        byId.keys.retainAll(liveIds)
        dirtyIds.clear()
        invalidateEveryTab = false
        selectedId = nextSelectedId
        if (next == published) return published
        published = next
        return published
    }

    fun clear() {
        byId.clear()
        dirtyIds.clear()
        invalidateEveryTab = true
        selectedId = null
        published = emptyList()
    }
}

/**
 * Projects the browser fields that can change independently for one tab row.
 *
 * A tab that has been nowhere reports no title or host so Chromium's internal
 * `about:blank` identity never reaches a card or screen reader. The explicit
 * fact still crosses the seam because an empty host can also describe a real
 * `data:` or `file:` page.
 */
internal fun Tab.toTaffyTab(
    selected: Boolean,
    assistantCreated: Boolean,
    creatingTaskId: String?,
): TaffyTab {
    val hasBeenNowhere = TaffyNavigationProjection.hasBeenNowhere(committedSpec)
    return TaffyTab(
        id = TaffyTabId(id.toString()),
        title = if (hasBeenNowhere) "" else title,
        host = if (hasBeenNowhere) "" else url.host,
        hasBeenNowhere = hasBeenNowhere,
        isTaffyTab = assistantCreated,
        isPrivate = isOffTheRecord,
        isSelected = selected,
        openedAtEpochMillis = timestampMillis,
        taskId = creatingTaskId?.takeIf { assistantCreated && !isOffTheRecord && it.isNotBlank() },
    )
}

/** Reads the selector structure once without the nested lists produced by flatMap/map. */
internal fun Iterable<TabModel>.currentTabs(): List<Tab> = buildList {
    for (model in this@currentTabs) {
        for (index in 0 until model.count) model.getTabAt(index)?.let(::add)
    }
}

/**
 * The address that has actually committed, falling back for a frozen restored tab.
 *
 * The visible URL changes to a pending navigation target before the renderer
 * paints it. Reading that target here would uncover the blank page too early.
 */
internal val Tab.committedSpec: String
    get() {
        val contents = webContents ?: return url.spec
        return contents.lastCommittedUrl.spec
    }
