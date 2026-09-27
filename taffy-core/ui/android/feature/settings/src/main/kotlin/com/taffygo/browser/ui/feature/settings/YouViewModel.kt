// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.preferences.LocalProfileRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Screen SCR-410's one source of truth. */
class YouViewModel(
    private val profile: LocalProfileRepository,
    time: TimeOnSitesRepository,
    memory: MemoryRepository,
    signIns: SavedSignInsRepository,
    details: SavedDetailsRepository,
    browser: BrowserRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val pane = MutableStateFlow(YouUiState())

    /** What screen SCR-410 renders. */
    val state: StateFlow<YouUiState> = combine(
        combine(time.snapshot, memory.snapshot, signIns.snapshot, ::Triple),
        combine(details.snapshot, browser.tabs, profile.profile, ::Triple),
        pane,
    ) { first, second, local ->
        projectYou(
            time = first.first,
            memory = first.second,
            signIns = first.third,
            details = second.first,
            tabs = second.second,
            profile = second.third,
            detailsOpen = local.detailsOpen,
            nameDraft = local.nameDraft,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = projectYou(
            time.snapshot.value,
            memory.snapshot.value,
            signIns.snapshot.value,
            details.snapshot.value,
            browser.tabs.value,
            profile.profile.value,
            pane.value.detailsOpen,
            pane.value.nameDraft,
        ),
    )

    /** Act on something the user did. */
    fun onIntent(intent: YouIntent, navigator: TaffyNavigator) {
        val typed = pane.value.nameDraft
        pane.value = reduceYou(pane.value, intent)
        when (intent) {
            // Closing the pane is as much a commit as pressing Done. A name
            // typed and then navigated away from is still a name somebody
            // meant to keep, and there is no other moment left to store it.
            YouIntent.CommitName, YouIntent.CloseDetails -> commitName(typed)
            YouIntent.OpenDetails -> Unit
            is YouIntent.EditName -> Unit
            is YouIntent.ChooseAvatar -> viewModelScope.launch {
                profile.setAvatar(intent.avatar)
            }
            is YouIntent.Open -> {
                if (intent.row == YouRow.PROFILE) return
                if (!youRowEnabled(intent.row, state.value)) return
                navigator.goTo(intent.row.destination)
            }
        }
    }

    /**
     * Store the typed name, then stop drawing it as a draft.
     *
     * The draft is cleared only after the write, because until then the
     * stored name is the old one and the field would flick back to it.
     */
    private fun commitName(typed: String?) {
        if (typed == null) return
        viewModelScope.launch {
            profile.setDisplayName(typed)
            pane.update { it.copy(nameDraft = null) }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.You.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}
