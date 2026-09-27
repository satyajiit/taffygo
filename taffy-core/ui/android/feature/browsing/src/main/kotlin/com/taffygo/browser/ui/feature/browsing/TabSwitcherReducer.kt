// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskRepositoryState

/**
 * The tasks that can still be holding a tab Taffy opened for them, or null
 * when the core cannot say which tasks it holds.
 *
 * A task holds its tabs until it has ended. Running, waiting for the person
 * and paused are all still going — a paused task resumes into the pages it was
 * reading, and a hand-over is a page the person is finishing for it. Done,
 * partly done, stopped and failed have ended, and a task the core no longer
 * lists at all — put away, ended before this browser run, or gone with a store
 * that was set aside — holds nothing.
 *
 * Only a ready core's list can say that. While the core is starting,
 * unavailable or waiting to be retried the list is empty because nothing was
 * read, not because nothing is running, so the answer is null and every tab
 * with a creating task is kept.
 */
internal fun TaskRepositoryState.tabHoldingTaskIds(): Set<String>? =
    if (availability != CoreUiAvailability.READY) {
        null
    } else {
        tasks.filterNot { it.displayState?.isFinal == true }.mapTo(HashSet()) { it.id }
    }

/**
 * The three groups screen SCR-104 shows, and the task facts its cards carry.
 *
 * [sources] and [taskIsRunning] come from the current task, which is the only
 * place a fact count exists: a tab knows what page it is on, and only the task
 * knows what it took from it. Excluded sources are left out, because a source
 * the user removed from scope no longer supports anything.
 *
 * [tabHoldingTaskIds] is every task that can still hold a tab, from
 * [TaskRepositoryState.tabHoldingTaskIds]. Null means the core could not say,
 * and keeps every tab that names a creating task — the default, because a
 * caller that says nothing about tasks has shown none of them ended.
 */
internal fun projectTabSwitcher(
    tabs: List<Tab>,
    sources: List<SourceRecord>,
    taskIsRunning: Boolean,
    group: TabSwitcherGroup,
    taffyGroupExpanded: Boolean,
    closeVisibleConfirmationVisible: Boolean = false,
    searchQuery: String = "",
    selectedForAsk: Set<TabId> = emptySet(),
    isSelecting: Boolean = false,
    artwork: Map<TabId, TabArtwork> = emptyMap(),
    siteMarks: Map<String, Bitmap> = emptyMap(),
    tabHoldingTaskIds: Set<String>? = null,
): TabSwitcherUiState {
    val factsByHost = HashMap<String, Int>()
    for (source in sources) {
        if (!source.excluded) {
            factsByHost[source.host] = (factsByHost[source.host] ?: 0)
                .saturatingAdd(source.factCount.coerceAtLeast(0))
        }
    }

    val keptSelection = LinkedHashSet<TabId>()
    val yourTabs = ArrayList<TabCard>()
    val privateTabs = ArrayList<TabCard>()
    val taffyTabs = ArrayList<TabCard>()

    fun card(tab: Tab): TabCard {
        val facts = if (tab.isTaffyTab) factsByHost[tab.host] ?: 0 else 0
        val art = artwork[tab.id]
        val canAsk = !tab.isTaffyTab && !tab.isPrivate &&
            !tab.hasBeenNowhere && tab.host.isNotBlank()
        // A Taffy tab with no creating task is a restored one, and no task in
        // this browser run can claim it (decision 0232), so nothing holds it.
        val creatingTask = tab.taskId
        val held = tab.isTaffyTab && creatingTask != null &&
            (tabHoldingTaskIds == null || creatingTask in tabHoldingTaskIds)
        return TabCard(
            id = tab.id,
            title = tab.title,
            host = tab.host,
            isSelected = tab.isSelected,
            openedByTaffy = tab.isTaffyTab,
            heldByTask = held,
            isPrivate = tab.isPrivate,
            factCount = facts,
            // A tab Taffy opened that has produced nothing yet, while the task
            // is still going, is a tab it is reading. Once the task stops, it
            // is a tab that produced nothing — which is not the same claim.
            isBeingRead = tab.isTaffyTab && taskIsRunning && facts == 0,
            openedAtEpochMillis = tab.openedAtEpochMillis,
            // The engine's in-memory mark first, then the profile's store —
            // the same two sources the start page's tiles read. A tab whose
            // `TabFavicon` was never constructed still has a mark here.
            favicon = art?.favicon ?: siteMarks[tab.host],
            thumbnail = art?.thumbnail,
            checkedForAsk = tab.id in selectedForAsk && canAsk,
        )
    }

    for (tab in tabs) {
        val projected = card(tab)
        if (projected.canAskAbout && tab.id in selectedForAsk) keptSelection += tab.id
        when {
            tab.isTaffyTab -> taffyTabs += projected
            tab.isPrivate -> privateTabs += projected
            else -> yourTabs += projected
        }
    }

    return TabSwitcherUiState(
        group = group,
        yourTabs = yourTabs,
        privateTabs = privateTabs,
        taffyTabs = taffyTabs,
        taffyGroupExpanded = taffyGroupExpanded,
        closeVisibleConfirmationVisible = closeVisibleConfirmationVisible,
        searchQuery = searchQuery,
        selectedForAsk = keptSelection,
        isSelecting = isSelecting && keptSelection.isNotEmpty(),
    )
}

private fun Int.saturatingAdd(other: Int): Int =
    if (this > Int.MAX_VALUE - other) Int.MAX_VALUE else this + other

/** What the purely local intents do. */
internal fun reduceTabSwitcher(
    state: TabSwitcherUiState,
    intent: TabSwitcherIntent,
): TabSwitcherUiState = when (intent) {
    TabSwitcherIntent.ToggleTaffyGroup -> state.copy(taffyGroupExpanded = !state.taffyGroupExpanded)
    is TabSwitcherIntent.SelectGroup -> state.copy(group = intent.group)
    is TabSwitcherIntent.Search -> state.copy(searchQuery = intent.query)
    TabSwitcherIntent.RequestCloseVisible -> state.copy(closeVisibleConfirmationVisible = true)
    TabSwitcherIntent.DismissCloseVisible,
    TabSwitcherIntent.ConfirmCloseVisible,
    -> state.copy(closeVisibleConfirmationVisible = false)
    is TabSwitcherIntent.LongPress -> {
        val card = state.findCard(intent.id) ?: return@reduceTabSwitcher state
        if (!card.canAskAbout) {
            state
        } else {
            state.withAskSelection(state.selectedForAsk + intent.id, selecting = true)
        }
    }
    is TabSwitcherIntent.ToggleSelected -> {
        if (!state.isSelecting) {
            state
        } else {
            val card = state.findCard(intent.id)
            if (card == null || !card.canAskAbout) {
                state
            } else {
                val next = if (intent.id in state.selectedForAsk) {
                    state.selectedForAsk - intent.id
                } else {
                    state.selectedForAsk + intent.id
                }
                state.withAskSelection(next, selecting = next.isNotEmpty())
            }
        }
    }
    TabSwitcherIntent.ClearSelection -> state.withAskSelection(emptySet(), selecting = false)
    is TabSwitcherIntent.Select,
    is TabSwitcherIntent.Close,
    TabSwitcherIntent.NewTab,
    TabSwitcherIntent.StartWorkspace,
    TabSwitcherIntent.AskTaffy,
    -> state
}

/**
 * Selection is stored twice: the id set, and a flag on each card so the grid
 * can draw and speak a chosen card without walking the set again.
 */
private fun TabSwitcherUiState.withAskSelection(
    selected: Set<TabId>,
    selecting: Boolean,
): TabSwitcherUiState {
    val eligibleIds = HashSet<TabId>(totalTabCount)
    fun List<TabCard>.collectEligibleIds() {
        for (card in this) {
            if (card.canAskAbout) eligibleIds += card.id
        }
    }
    yourTabs.collectEligibleIds()
    privateTabs.collectEligibleIds()
    taffyTabs.collectEligibleIds()
    val kept = selected.filterTo(LinkedHashSet()) { it in eligibleIds }
    fun List<TabCard>.marked(): List<TabCard> =
        map { card ->
            val checked = card.id in kept
            if (checked == card.checkedForAsk) card else card.copy(checkedForAsk = checked)
        }
    return copy(
        yourTabs = yourTabs.marked(),
        privateTabs = privateTabs.marked(),
        taffyTabs = taffyTabs.marked(),
        selectedForAsk = kept,
        isSelecting = selecting && kept.isNotEmpty(),
    )
}

private fun TabSwitcherUiState.findCard(id: TabId): TabCard? =
    yourTabs.firstOrNull { it.id == id }
        ?: privateTabs.firstOrNull { it.id == id }
        ?: taffyTabs.firstOrNull { it.id == id }
