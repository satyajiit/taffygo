// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.annotation.StringRes
import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.ui.TaffyIcon

/** One store's own word, wherever the box names it: the plus, and the chip. */
@StringRes
internal fun storeLabel(store: TaskAttachedStore): Int = when (store) {
    TaskAttachedStore.HISTORY -> R.string.taffy_browser_menu_history
    TaskAttachedStore.BOOKMARKS -> R.string.taffy_browser_menu_bookmarks
    TaskAttachedStore.OPEN_TABS -> R.string.taffy_start_store_open_tabs
}

/** The glyph the browsing menu already gives each store, so the two agree. */
internal fun storeIcon(store: TaskAttachedStore): ImageVector = when (store) {
    TaskAttachedStore.HISTORY -> TaffyIcon.ClockCounterClockwise
    TaskAttachedStore.BOOKMARKS -> TaffyIcon.BookmarkSimple
    TaskAttachedStore.OPEN_TABS -> TaffyIcon.Browsers
}

/** The store as the consent sentence names it: the person's own, in a phrase. */
@StringRes
internal fun storeConsentName(store: TaskAttachedStore): Int = when (store) {
    TaskAttachedStore.HISTORY -> R.string.taffy_task_start_store_history
    TaskAttachedStore.BOOKMARKS -> R.string.taffy_task_start_store_bookmarks
    TaskAttachedStore.OPEN_TABS -> R.string.taffy_task_start_store_open_tabs
}

/** One store tile in the plus's menu, and one store chip under the box, by the store's label. */
const val START_STORE_TILE_TEST_TAG_PREFIX: String = "start_menu_store_"
const val START_STORE_CHIP_TEST_TAG_PREFIX: String = "start_store_"
