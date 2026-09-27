// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffyPageChipState

/**
 * One page on Ask: a tab the person pointed at.
 *
 * Closed pages stay until they are removed. Taffy-opened pages are read-only
 * and never offered on a new ask.
 */
data class AttachedPage(
    val tabId: TabId,
    val title: String,
    val host: String,
    val closed: Boolean = false,
    val taffyOpened: Boolean = false,
) {
    /** The chip this page draws as. Marks stay local; this carries none. */
    fun toChipState(): TaffyPageChipState = TaffyPageChipState(
        title = title,
        host = host,
        closed = closed,
        taffyOpened = taffyOpened,
        removable = !taffyOpened,
    )
}

/** An eligible user tab as a page the person can point at. */
internal fun Tab.toAttachedPage(): AttachedPage = AttachedPage(
    tabId = id,
    title = title,
    host = host,
    closed = false,
    taffyOpened = isTaffyTab,
)
