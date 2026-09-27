// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-601's one source of truth. */
class SkillsListViewModel @Inject constructor(
    private val skills: SkillsRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val query = MutableStateFlow(savedState.get<String>(QUERY_KEY).orEmpty())

    /** What screen SCR-601 renders. */
    val state: StateFlow<SkillsListUiState> = combine(skills.snapshot, query) { snapshot, q ->
        projectSkillsList(snapshot, q)
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = projectSkillsList(skills.snapshot.value, query.value),
    )

    /** Act on something the user did. */
    fun onIntent(intent: SkillsListIntent, navigator: TaffyNavigator) {
        val reduced = reduceSkillsList(state.value, intent)
        query.value = reduced.query
        savedState[QUERY_KEY] = reduced.query
        when (intent) {
            is SkillsListIntent.QueryChanged -> Unit
            is SkillsListIntent.Toggle -> viewModelScope.launch {
                val current = skills.snapshot.value.skills.firstOrNull { it.id == intent.id }
                    ?: return@launch
                if (current.needsRecordedReview) {
                    navigator.goTo(TaffyDestination.SkillDetail(intent.id))
                } else {
                    skills.setEnabled(intent.id, !current.enabled)
                }
            }
            is SkillsListIntent.Open ->
                navigator.goTo(TaffyDestination.SkillDetail(intent.id))
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.SkillsList.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val QUERY_KEY = "skills_query"
    }
}
