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
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-403's one source of truth. */
class PrivacyViewModel(
    private val privacy: PrivacyCenterRepository,
    private val preferences: UserPreferencesRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {
    private val operation = MutableStateFlow(PrivacyUiState())

    val state: StateFlow<PrivacyUiState> = combine(
        preferences.preferences,
        privacy.snapshot,
        operation,
        ::projectPrivacyUiState,
    ).stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = projectPrivacyUiState(
            preferences.preferences.value,
            privacy.snapshot.value,
            operation.value,
        ),
    )

    fun onIntent(intent: PrivacyIntent, navigator: TaffyNavigator) {
        operation.value = reducePrivacy(currentState(), intent)
        when (intent) {
            PrivacyIntent.OpenSiteSettings -> navigator.goTo(TaffyDestination.SiteSettings)
            PrivacyIntent.OpenClearData -> navigator.goTo(TaffyDestination.ClearBrowsingData)
            PrivacyIntent.OpenWhatHappened -> navigator.goTo(TaffyDestination.WhatHappened)
            PrivacyIntent.Dismiss -> navigator.goBack()
            PrivacyIntent.ConfirmDeleteEverything -> {
                if (operation.value.deletionStatus == PrivacyUiState.DeletionStatus.STARTING) {
                    viewModelScope.launch {
                        val result = privacy.deleteEverything()
                        operation.value = reducePrivacy(
                            currentState(),
                            PrivacyIntent.DeleteFinished(result),
                        )
                    }
                }
            }
            PrivacyIntent.RequestExport,
            PrivacyIntent.ExportDestinationCancelled,
            PrivacyIntent.ExportDestinationFailed,
            PrivacyIntent.ExportDestinationSelected,
            is PrivacyIntent.ExportFinished,
            PrivacyIntent.RequestDeleteEverything,
            PrivacyIntent.DismissDeleteConfirmation,
            is PrivacyIntent.DeleteFinished,
            -> Unit
        }
    }

    fun writeExport(write: suspend (ByteArray) -> Boolean) {
        if (operation.value.exportStatus != PrivacyUiState.ExportStatus.CHOOSING_DESTINATION) return
        operation.value = reducePrivacy(currentState(), PrivacyIntent.ExportDestinationSelected)
        viewModelScope.launch {
            val result = privacy.exportEverything(write)
            operation.value = reducePrivacy(currentState(), PrivacyIntent.ExportFinished(result))
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.Privacy.screenId))
    }

    private fun currentState() = projectPrivacyUiState(
        preferences.preferences.value,
        privacy.snapshot.value,
        operation.value,
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

internal fun projectPrivacyUiState(
    preferences: UserPreferences,
    privacy: PrivacyCenterRepository.Snapshot,
    operation: PrivacyUiState = PrivacyUiState(),
) = PrivacyUiState(
    exportAvailable = privacy.exportAvailable,
    deleteAvailable = privacy.deleteAvailable,
    route = preferences.providerRoute,
    savedWorkspaces = privacy.savedWorkspaces,
    libraryItems = privacy.libraryItems,
    memoryItems = privacy.memoryItems,
    connectedProviders = privacy.connectedProviders,
    recentDownloads = privacy.recentDownloads,
    changedSites = privacy.changedSites,
    exportStatus = operation.exportStatus,
    deletionStatus = operation.deletionStatus,
)
