// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

/** Browser-reported pages associated with one exact task, with local engine artwork only. */
data class TaskBrowserUiState(
    val taskId: String? = null,
    val tabs: List<Tab> = emptyList(),
    val artwork: Map<TabId, TabArtwork> = emptyMap(),
    /**
     * The mark of every host this task cited, keyed by host.
     *
     * From the profile's own favicon store and nowhere else — the same store
     * the start page's tiles read, never a network fetch, so a source list
     * shows a site's real mark without this screen ever reaching a server the
     * task did not already visit. A host with no stored mark is simply absent,
     * and the row draws its citation number instead.
     */
    val siteMarks: Map<String, Bitmap> = emptyMap(),
)

/** A missing ownership fact never becomes a guess from a host or the assistant-created flag. */
internal fun projectTaskBrowser(
    taskId: String?,
    tabs: List<Tab>,
    artwork: Map<TabId, TabArtwork>,
    acceptedSources: Set<TabId> = emptySet(),
): TaskBrowserUiState {
    if (taskId.isNullOrBlank()) return TaskBrowserUiState()
    val associated = tabs.filter { (it.taskId == taskId || it.id in acceptedSources) && !it.isPrivate }.distinctBy { it.id }
    val pictures = buildMap {
        associated.forEach { tab -> artwork[tab.id]?.let { put(tab.id, it) } }
    }
    return TaskBrowserUiState(taskId, associated, pictures)
}
