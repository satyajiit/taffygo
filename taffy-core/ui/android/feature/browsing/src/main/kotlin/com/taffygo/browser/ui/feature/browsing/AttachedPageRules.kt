// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

/**
 * How the pages an ask is about follow the tab list.
 *
 * Pure functions over the browser's own tab list, so the box's reducer and a
 * host test read the same rule. Every one of them only ever narrows or
 * refreshes: none can attach a tab the person did not point at (decisions
 * 0034 and 0035, applied by [isEligibleAskTab]).
 */

/**
 * Keep closed chips the sheet cannot show, and replace the live set with the
 * ticked open tabs.
 */
internal fun pagesAfterAttachConfirm(
    eligible: List<Tab>,
    ticked: List<TabId>,
    previously: List<AttachedPage>,
): List<AttachedPage> {
    val wanted = ticked.toSet()
    val selected = eligible.filter { it.id in wanted }.map { it.toAttachedPage() }
    val selectedIds = selected.map { it.tabId }.toSet()
    val closedKept = previously.filter { page ->
        page.closed && page.tabId !in selectedIds
    }
    return selected + closedKept
}

/** Refresh titles and closed flags from the live tab list. Never drops a chip. */
internal fun refreshAttachedPages(
    attached: List<AttachedPage>,
    tabs: List<Tab>,
): List<AttachedPage> {
    val live = tabs.associateBy { it.id }
    return attached.map { page ->
        val tab = live[page.tabId]
        if (tab == null) {
            page.copy(closed = true)
        } else {
            page.copy(
                title = tab.title.ifBlank { page.title },
                host = tab.host.ifBlank { page.host },
                closed = false,
                taffyOpened = tab.isTaffyTab,
            )
        }
    }
}

/**
 * The one tab an ask with no named ids is about, or null when the projection
 * names no single selected tab.
 *
 * One function because three callers had the same two lines and only two of
 * them could answer "is this tab Taffy's?" — which is the question the Ask
 * overlay has to answer before it tells a person to go and choose pages.
 * Eligibility is deliberately not applied here: the tab a person is looking at
 * is a fact, and whether it may become a source is the next question.
 */
internal fun currentAskTab(tabs: List<Tab>): Tab? = tabs.singleOrNull(Tab::isSelected)

/** Current eligible user tab, or nothing. Never private, nowhere, or Taffy's. */
internal fun defaultAttachedPages(tabs: List<Tab>): List<AttachedPage> {
    val current = currentAskTab(tabs) ?: return emptyList()
    if (!isEligibleAskTab(current)) return emptyList()
    return listOf(current.toAttachedPage())
}

/** Named ids that are still eligible, in the order they were named. */
internal fun attachedPagesFromIds(tabs: List<Tab>, ids: List<TabId>): List<AttachedPage> {
    val byId = tabs.associateBy { it.id }
    return ids.mapNotNull { id ->
        val tab = byId[id]
        when {
            tab == null -> AttachedPage(tabId = id, title = "", host = "", closed = true)
            isEligibleAskTab(tab) -> tab.toAttachedPage()
            else -> null
        }
    }
}
