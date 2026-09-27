// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One browser tab as the Taffy-owned surfaces see it (UX spec section 2).
 *
 * Taffy's tabs are a separate, collapsed group in the switcher, so the
 * distinction is a property of the tab rather than of the screen that draws it.
 */
data class Tab(
    /** Identity of the tab. */
    val id: TabId,
    /** The page title as the page gave it. */
    val title: String,
    /** The host, shown instead of a full URL. */
    val host: String,
    /**
     * Whether this tab has not been anywhere at all.
     *
     * Carried rather than inferred from an empty [host], because the two are
     * not the same question and only one of them has a safe wrong answer. A
     * page served from a `data:` URL or a `file:` path has no host either, and
     * some of those have no title to fall back on — so a surface that read
     * blankness as "nothing here" would draw TaffyGo's own content over a page
     * the web actually served. The mediator behind this seam already knows
     * which it is, because the engine told it; the fact only had nowhere to go.
     *
     * `false` is the default, and it is the closed answer for this one: a tab
     * constructed without an answer is a tab that has a page, so nothing is
     * ever drawn over content nobody checked was absent.
     */
    val hasBeenNowhere: Boolean = false,
    /** Whether Taffy opened this tab for a task. */
    val isTaffyTab: Boolean = false,
    /** Whether the tab forgets everything when it closes. */
    val isPrivate: Boolean = false,
    /** Whether this is the tab the user is looking at. */
    val isSelected: Boolean = false,
    /**
     * When this tab was opened, milliseconds since the Unix epoch.
     *
     * Zero means the mediator did not know. The switcher hides the age chip in
     * that case rather than inventing a duration.
     */
    val openedAtEpochMillis: Long = 0,
    /** The task that created this tab; separate from its current membership in task sources. */
    val taskId: String? = null,
)
