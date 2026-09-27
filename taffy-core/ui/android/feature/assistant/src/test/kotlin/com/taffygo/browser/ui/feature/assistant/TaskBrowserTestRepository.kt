// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow

/** Only the browser's live membership and selection answers are controlled here. */
internal class TaskBrowserTestRepository : BrowserRepository {
    override val tabs = MutableStateFlow<List<Tab>>(emptyList())
    override val navigation = MutableStateFlow(NavigationState(host = "", title = ""))
    override val pageAppearance = MutableStateFlow(PageAppearance())
    override val downloads = MutableStateFlow<List<DownloadRecord>>(emptyList())
    override val tabArtwork = MutableStateFlow<Map<TabId, TabArtwork>>(emptyMap())
    override val siteMarks = MutableStateFlow<Map<String, Bitmap>>(emptyMap())
    override val notice = MutableStateFlow<BrowserNotice?>(null)
    override val filtering = MutableStateFlow(FilteringSettings())
    val queries = mutableListOf<String>()
    val selections = mutableListOf<Pair<TabId, String>>()
    var members: suspend (String) -> Set<TabId> = { emptySet() }
    var selectionAccepted = false
    val fileQueries = mutableListOf<String>()
    val fileOpens = mutableListOf<Pair<String, DownloadId>>()
    var completedFiles: suspend (String) -> List<DownloadRecord> = { emptyList() }
    var openFile: suspend (String, DownloadId) -> Boolean = { _, _ -> false }

    override suspend fun completedTaskDownloads(taskId: String): List<DownloadRecord> {
        fileQueries += taskId
        return completedFiles(taskId)
    }

    override suspend fun openTaskDownload(taskId: String, id: DownloadId): Boolean {
        fileOpens += taskId to id
        return openFile(taskId, id)
    }

    override suspend fun tabsForTask(taskId: String): Set<TabId> {
        queries += taskId
        return members(taskId)
    }

    override suspend fun selectTaskTab(id: TabId, taskId: String): Boolean {
        selections += id to taskId
        return selectionAccepted
    }

    override fun resolve(input: String): AddressBarInterpretation = error("No address entry")
    override fun suggestions(input: String): List<Suggestion> = error("No suggestions")
    override suspend fun commit(interpretation: AddressBarInterpretation): Unit = error("No navigation")
    override fun dismissNotice(): Unit = error("No notices")
    override suspend fun selectTab(id: TabId): Unit = error("Task cards require task admission")
    override suspend fun closeTab(id: TabId): Unit = error("No tab closure")
    override suspend fun openTab(host: String, isPrivate: Boolean): TabId = error("No new tabs")
    override suspend fun goBack(): Boolean = error("No history navigation")
    override suspend fun goForward(): Boolean = error("No history navigation")
    override suspend fun reload(): Unit = error("No page loads")
    override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction): Boolean = error("No downloads")
    override suspend fun requestSiteMarks(hosts: Collection<String>): Unit = error("No artwork request")
    override suspend fun setFilteringEnabled(enabled: Boolean): Unit = error("No filtering mutation")
    override suspend fun setSiteFilteringException(host: String, allow: Boolean, plane: SiteFilteringPlane): Boolean =
        error("No filtering mutation")
    override suspend fun flushFilteringCounts(): Unit = error("No filtering query")
}
