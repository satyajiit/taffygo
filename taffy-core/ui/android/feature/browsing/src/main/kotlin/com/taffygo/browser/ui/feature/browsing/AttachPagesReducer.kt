// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

/**
 * Search and tick on the Add pages sheet. Confirm and dismiss change no
 * state here: the parent copies the ticked set, or does not.
 */
internal fun reduceAttachPages(
    state: AttachPagesUiState,
    intent: AttachPagesIntent,
): AttachPagesUiState = when (intent) {
    is AttachPagesIntent.SearchChanged -> state.copy(query = intent.query)
    is AttachPagesIntent.Toggle -> {
        val next = if (intent.tabId in state.tickedIds) {
            state.tickedIds - intent.tabId
        } else {
            state.tickedIds + intent.tabId
        }
        state.copy(tickedIds = next, rows = state.rows.withTicks(next))
    }
    AttachPagesIntent.Confirm,
    AttachPagesIntent.Dismiss,
    -> state
}

/** Rebuild the sheet from a fresh listing, keeping ticks that still exist. */
internal fun attachPagesFromSnapshot(
    snapshot: AskPagesSnapshot,
    alreadyAttached: Collection<TabId>,
    query: String = "",
    previousTicks: Set<TabId>? = null,
    artwork: Map<TabId, TabArtwork> = emptyMap(),
    siteMarks: Map<String, Bitmap> = emptyMap(),
): AttachPagesUiState {
    val ticked = when (snapshot.status) {
        AskPagesSnapshot.Status.LOADING,
        AskPagesSnapshot.Status.UNAVAILABLE,
        -> previousTicks ?: alreadyAttached.toSet()
        AskPagesSnapshot.Status.READY -> {
            val liveIds = snapshot.eligibleTabs.map { it.id }.toSet()
            val base = previousTicks ?: alreadyAttached.toSet()
            base.filter { it in liveIds }.toSet()
        }
    }
    return AttachPagesUiState(
        status = snapshot.status,
        query = query,
        rows = snapshot.eligibleTabs.toRows(ticked, artwork, siteMarks),
        tickedIds = ticked,
        taffyTabsPresent = snapshot.taffyTabsPresent,
    )
}

private fun List<Tab>.toRows(
    ticked: Set<TabId>,
    artwork: Map<TabId, TabArtwork>,
    siteMarks: Map<String, Bitmap>,
): List<AttachPagesUiState.Row> =
    map { tab ->
        AttachPagesUiState.Row(
            tabId = tab.id,
            title = tab.title.ifBlank { tab.host },
            host = tab.host,
            ticked = tab.id in ticked,
            favicon = artwork[tab.id]?.favicon ?: siteMarks[tab.host],
        )
    }

private fun List<AttachPagesUiState.Row>.withTicks(
    ticked: Set<TabId>,
): List<AttachPagesUiState.Row> = map { row -> row.copy(ticked = row.tabId in ticked) }
