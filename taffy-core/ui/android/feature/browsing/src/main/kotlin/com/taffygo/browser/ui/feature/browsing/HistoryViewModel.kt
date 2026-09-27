// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import java.time.ZoneId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-201's one source of truth. */
class HistoryViewModel(
    private val history: HistoryRepository,
    private val browser: BrowserRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
    private val nowEpochMillis: () -> Long = System::currentTimeMillis,
    private val zone: ZoneId = ZoneId.systemDefault(),
) : ViewModel() {

    private val query = MutableStateFlow(savedState.get<String>(QUERY_KEY).orEmpty())

    /** What screen SCR-201 renders. */
    val state: StateFlow<HistoryUiState> =
        combine(history.snapshot, query, browser.siteMarks) { snapshot, queryText, marks ->
            projectHistory(snapshot, queryText, nowEpochMillis(), zone).copy(siteMarks = marks)
        }
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectHistory(
                    history.snapshot.value,
                    query.value,
                    nowEpochMillis(),
                    zone,
                ).copy(siteMarks = browser.siteMarks.value),
            )

    init {
        history.snapshot
            .onEach { snapshot ->
                val ready = snapshot as? HistorySnapshot.Ready ?: return@onEach
                browser.requestSiteMarks(ready.visits.forPerson().map { it.host })
            }
            .launchIn(viewModelScope)
    }

    /** Act on something the user did. */
    fun onIntent(intent: HistoryIntent, navigator: TaffyNavigator) {
        when (intent) {
            is HistoryIntent.QueryChanged -> {
                query.value = intent.query
                savedState[QUERY_KEY] = intent.query
            }
            is HistoryIntent.Open -> viewModelScope.launch { open(intent.id, navigator) }
            is HistoryIntent.Delete -> viewModelScope.launch { history.delete(intent.id) }
            HistoryIntent.Clear -> navigator.goTo(TaffyDestination.ClearBrowsingData)
            HistoryIntent.Dismiss -> navigator.goBack()
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.History.screenId))
    }

    private suspend fun open(id: HistoryVisit.Id, navigator: TaffyNavigator) {
        if (!history.open(id)) return
        navigator.replaceCurrent(TaffyDestination.BrowserMain)
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val QUERY_KEY = "history_query"
    }
}
