// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.model.TabId

/**
 * One card of screen SCR-104's grid.
 *
 * A card rather than the `Tab` it came from, because two of the four things the
 * handoff draws on a card — how many facts Taffy has taken from that page, and
 * whether it is reading it right now — are properties of the task rather than
 * of the tab. Projecting them once into this shape keeps the grid from asking
 * the task anything while it draws.
 */
data class TabCard(
    /** Identity of the tab this card stands for. */
    val id: TabId,
    /** The page title as the page gave it. */
    val title: String,
    /** The host, shown under the preview. */
    val host: String,
    /** Whether this is the tab the user is looking at. */
    val isSelected: Boolean = false,
    /** Whether Taffy opened this tab for a task — the amber edge. */
    val openedByTaffy: Boolean = false,
    /**
     * Whether a task that has not ended is holding this tab, so closing every
     * tab leaves it open.
     *
     * Not the same fact as [openedByTaffy]. A tab restored after a restart is
     * Taffy's with no creating task (decision 0232), and a tab whose task has
     * ended belongs to nothing that could still cite it. The close sheet's
     * promise is about a running task, so only a tab one still holds is kept
     * (decision 0236).
     */
    val heldByTask: Boolean = false,
    /** Whether the tab forgets everything when it closes — the violet edge. */
    val isPrivate: Boolean = false,
    /** How many facts the current task has taken from this host. */
    val factCount: Int = 0,
    /** Whether Taffy is reading this page right now. */
    val isBeingRead: Boolean = false,
    /** When the tab was opened, or zero when the engine did not say. */
    val openedAtEpochMillis: Long = 0,
    /** The site's mark from the engine, if it has one. */
    val favicon: Bitmap? = null,
    /** A local page snapshot, if one has been captured. */
    val thumbnail: Bitmap? = null,
    /** Whether this card is in the Ask Taffy selection. */
    val checkedForAsk: Boolean = false,
) {
    /**
     * Whether the card carries the amber badge at all.
     *
     * A tab Taffy opened before the task started reading has nothing to report,
     * and "0 facts" is a worse answer than no badge.
     */
    val hasBadge: Boolean
        get() = isBeingRead || factCount > 0

    /**
     * Whether this tab may be a source for Ask Taffy.
     *
     * Decision 0034: a tab that has been nowhere names no origin. Decision
     * 0035: a private tab is not offered. Taffy's own tabs are not the
     * person's to offer.
     */
    val canAskAbout: Boolean
        get() = host.isNotBlank() && !openedByTaffy && !isPrivate

    /**
     * Whether this card is a tab that has not been anywhere.
     *
     * A duration chip and a page snapshot would be inventing a history the
     * tab does not have. The start page's own weather belongs here instead.
     */
    val hasBeenNowhere: Boolean
        get() = title.isBlank() && host.isBlank()
}
