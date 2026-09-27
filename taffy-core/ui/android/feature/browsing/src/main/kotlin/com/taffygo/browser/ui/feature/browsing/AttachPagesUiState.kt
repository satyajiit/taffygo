// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.model.TabId

/**
 * Add pages sheet (SCR-810): open user tabs, search, tick, confirm.
 *
 * Taffy's tabs and private tabs are never rows. More than eight is a caution,
 * not a block.
 */
data class AttachPagesUiState(
    val status: AskPagesSnapshot.Status = AskPagesSnapshot.Status.LOADING,
    val query: String = "",
    val rows: List<Row> = emptyList(),
    val tickedIds: Set<TabId> = emptySet(),
    val taffyTabsPresent: Boolean = false,
) {
    /** Rows matching the search, or every row when the search is empty. */
    val visibleRows: List<Row>
        get() {
            val needle = query.trim()
            if (needle.isEmpty()) return rows
            return rows.filter { row ->
                row.title.contains(needle, ignoreCase = true) ||
                    row.host.contains(needle, ignoreCase = true)
            }
        }

    /** Whether more than eight pages are ticked. */
    val cautionOverHandful: Boolean
        get() = tickedIds.size > HANDFUL_CAUTION

    /** One open user tab the person may tick. */
    data class Row(
        val tabId: TabId,
        val title: String,
        val host: String,
        val ticked: Boolean,
        val favicon: Bitmap? = null,
    )

    private companion object {
        const val HANDFUL_CAUTION: Int = 8
    }
}
