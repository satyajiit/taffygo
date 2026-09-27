// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.mapLatest
import kotlinx.coroutines.flow.runningFold
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** SCR-203 state owner; indexing and every organizer projection stay off the render thread. */
@OptIn(ExperimentalCoroutinesApi::class)
class DownloadsViewModel(
    private val downloads: DownloadRepository,
    private val parts: TaffyPartsRepository,
    private val analytics: AnalyticsClient,
    private val dispatchers: AppDispatchers,
) : ViewModel() {
    private val controls = MutableStateFlow(DownloadOrganizerControls())
    private val tab = MutableStateFlow(DownloadsTab.YOURS)
    private val actionNotice = MutableStateFlow<DownloadActionNotice?>(null)

    private val liveProgress: StateFlow<Map<TaffyPartId, TaffyPartProgress>> = parts.progress
        .runningFold(emptyMap<TaffyPartId, TaffyPartProgress>()) { seen, report ->
            seen + (report.id to report)
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = emptyMap(),
        )

    private val indexed = downloads.snapshot
        .mapLatest { snapshot ->
            withContext(dispatchers.default) {
                IndexedDownloadSnapshot(
                    status = snapshot.status,
                    index = DownloadOrganizerIndex.build(snapshot.downloads),
                )
            }
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = EMPTY_INDEX,
        )

    private val organized = combine(indexed, controls, ::OrganizerInputs)
        .mapLatest { input ->
            withContext(dispatchers.default) {
                val selected = input.controls
                ProjectedDownloadCollection(
                    query = selected.query,
                    filter = selected.filter,
                    sort = selected.sort,
                    grouping = selected.grouping,
                    status = input.indexed.status,
                    projection = input.indexed.index.project(
                        query = selected.query,
                        filter = selected.filter,
                        sort = selected.sort,
                        grouping = selected.grouping,
                    ),
                )
            }
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = EMPTY_COLLECTION,
        )

    private val baseState = combine(organized, parts.state, liveProgress, tab) {
            organizer, partsState, progress, selectedTab ->
        projectDownloads(organizer, partsState, progress, selectedTab)
    }

    val state: StateFlow<DownloadsUiState> = combine(baseState, actionNotice) { projected, notice ->
        projected.copy(actionNotice = notice)
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = DownloadsUiState(),
    )

    fun onIntent(intent: DownloadsIntent) {
        when (intent) {
            is DownloadsIntent.SetQuery -> controls.update {
                it.copy(query = boundedDownloadQuery(intent.query))
            }
            is DownloadsIntent.SelectFilter -> controls.update { it.copy(filter = intent.filter) }
            is DownloadsIntent.SelectSort -> controls.update { it.copy(sort = intent.sort) }
            is DownloadsIntent.SelectGrouping -> controls.update {
                it.copy(grouping = intent.grouping)
            }
            is DownloadsIntent.SelectTab -> tab.value = intent.tab
            DownloadsIntent.DismissActionNotice -> actionNotice.value = null
            is DownloadsIntent.RemovePart -> viewModelScope.launch { parts.remove(intent.id) }
            is DownloadsIntent.Pause -> perform(intent.id, DownloadAction.PAUSE)
            is DownloadsIntent.Resume -> perform(intent.id, DownloadAction.RESUME)
            is DownloadsIntent.Cancel -> perform(intent.id, DownloadAction.CANCEL)
            is DownloadsIntent.Open -> perform(intent.id, DownloadAction.OPEN)
            is DownloadsIntent.Share -> perform(intent.id, DownloadAction.SHARE)
            is DownloadsIntent.Remove -> perform(intent.id, DownloadAction.REMOVE)
        }
    }

    private fun perform(id: DownloadId, action: DownloadAction) {
        actionNotice.value = null
        viewModelScope.launch {
            val accepted = try {
                downloads.perform(id, action)
            } catch (_: RuntimeException) {
                false
            }
            if (!accepted) actionNotice.value = DownloadActionNotice.NO_LONGER_AVAILABLE
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.Downloads.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        val EMPTY_INDEX = IndexedDownloadSnapshot(
            status = DownloadCollectionStatus.LOADING,
            index = DownloadOrganizerIndex.build(emptyList()),
        )
        val EMPTY_COLLECTION = ProjectedDownloadCollection(
            query = "",
            filter = DownloadFilter.ALL,
            sort = DownloadSort.NEWEST,
            grouping = DownloadGrouping.NONE,
            status = DownloadCollectionStatus.LOADING,
            projection = DownloadOrganizerProjection(emptyList(), 0, 0),
        )
    }
}

private data class DownloadOrganizerControls(
    val query: String = "",
    val filter: DownloadFilter = DownloadFilter.ALL,
    val sort: DownloadSort = DownloadSort.NEWEST,
    val grouping: DownloadGrouping = DownloadGrouping.NONE,
)

private data class IndexedDownloadSnapshot(
    val status: DownloadCollectionStatus,
    val index: DownloadOrganizerIndex,
)

private data class OrganizerInputs(
    val indexed: IndexedDownloadSnapshot,
    val controls: DownloadOrganizerControls,
)
