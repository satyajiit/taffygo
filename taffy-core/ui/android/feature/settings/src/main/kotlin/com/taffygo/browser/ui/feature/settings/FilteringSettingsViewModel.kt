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
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-206's one source of truth. */
class FilteringSettingsViewModel(
    private val browser: BrowserRepository,
    week: BlockingWeekRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val removal = MutableStateFlow(FilteringSettingsUiState.Removal())
    private var removalGeneration = 0L

    /** What screen SCR-206 renders. */
    val state: StateFlow<FilteringSettingsUiState> =
        combine(browser.filtering, week.snapshot, removal, ::projectFilteringSettings)
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectFilteringSettings(
                    browser.filtering.value,
                    week.snapshot.value,
                    removal.value,
                ),
            )

    /** Act on something the user did. */
    fun onIntent(intent: FilteringSettingsIntent) {
        when (intent) {
            is FilteringSettingsIntent.SetEnabled -> viewModelScope.launch {
                browser.setFilteringEnabled(intent.enabled)
            }
            is FilteringSettingsIntent.RemoveException -> removeException(intent.host)
        }
    }

    /**
     * Blocks a site again, on the **regular** profile's list.
     *
     * The plane is named rather than followed from the selected tab: a private
     * tab selected behind this screen must not capture a write meant for the
     * profile, and a private tab's allowances are not on this screen to remove
     * (decision 0128).
     */
    private fun removeException(host: String) {
        if (host.isBlank() ||
            removal.value.status == FilteringSettingsUiState.Removal.Status.RUNNING
        ) {
            return
        }
        val request = ++removalGeneration
        removal.value = FilteringSettingsUiState.Removal(
            host = host,
            status = FilteringSettingsUiState.Removal.Status.RUNNING,
        )
        viewModelScope.launch {
            val recorded = try {
                browser.setSiteFilteringException(
                    host,
                    allow = false,
                    plane = SiteFilteringPlane.PROFILE,
                )
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: RuntimeException) {
                false
            }
            if (request != removalGeneration) return@launch
            removal.value = FilteringSettingsUiState.Removal(
                host = host,
                status = if (recorded) {
                    FilteringSettingsUiState.Removal.Status.SUCCEEDED
                } else {
                    FilteringSettingsUiState.Removal.Status.FAILED
                },
            )
        }
    }

    /** Record that this screen was shown. The identifier, never the content. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.AdAndTrackerBlocking.screenId),
        )
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}
