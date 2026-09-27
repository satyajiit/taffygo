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
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-205's one source of truth. */
class SiteSettingsViewModel(
    private val sites: SiteSettingsRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {
    private val presentation = MutableStateFlow(Presentation())

    val state: StateFlow<SiteSettingsUiState> = combine(
        sites.snapshot,
        presentation,
    ) {
            snapshot,
            presentation,
        ->
            projectSiteSettings(snapshot).copy(
                updating = presentation.updating,
                failedUpdate = presentation.failedUpdate,
                resetCandidate = presentation.resetCandidate.takeIf { host ->
                    snapshot is SiteSettingsRepository.Snapshot.Ready &&
                        snapshot.sites.any { it.host == host }
                },
                resettingHost = presentation.resettingHost,
                failedResetHost = presentation.failedResetHost,
            )
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectSiteSettings(sites.snapshot.value),
        )

    fun onIntent(intent: SiteSettingsIntent, navigator: TaffyNavigator) {
        val current = state.value
        val reduced = reduceSiteSettings(current, intent)
        presentation.value = reduced.toPresentation()
        when (intent) {
            SiteSettingsIntent.Retry -> sites.refresh()
            SiteSettingsIntent.Dismiss -> if (current.resetCandidate == null) {
                navigator.goBack()
            }
            is SiteSettingsIntent.SetDefault -> if (
                current.updating == null && reduced.updating == intent.capability
            ) {
                viewModelScope.launch {
                    val result = sites.setDefault(intent.capability, intent.enabled)
                    presentation.value = presentation.value.copy(
                        updating = null,
                        failedUpdate = intent.capability.takeUnless {
                            result == SiteSettingsRepository.UpdateResult.APPLIED
                        },
                    )
                }
            }
            SiteSettingsIntent.ConfirmSiteReset -> if (
                current.resetCandidate != null &&
                reduced.resettingHost == current.resetCandidate
            ) {
                viewModelScope.launch { resetSite(current.resetCandidate) }
            }
            SiteSettingsIntent.DismissSiteReset,
            is SiteSettingsIntent.RequestSiteReset,
            -> Unit
        }
    }

    private suspend fun resetSite(host: String) {
        val result = sites.resetSite(host)
        presentation.value = presentation.value.copy(
            resettingHost = null,
            failedResetHost = host.takeUnless {
                result == SiteSettingsRepository.UpdateResult.APPLIED
            },
        )
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.SiteSettings.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }

    private data class Presentation(
        val updating: SiteSettingsRepository.Capability? = null,
        val failedUpdate: SiteSettingsRepository.Capability? = null,
        val resetCandidate: String? = null,
        val resettingHost: String? = null,
        val failedResetHost: String? = null,
    )

    private fun SiteSettingsUiState.toPresentation() = Presentation(
        updating = updating,
        failedUpdate = failedUpdate,
        resetCandidate = resetCandidate,
        resettingHost = resettingHost,
        failedResetHost = failedResetHost,
    )
}
