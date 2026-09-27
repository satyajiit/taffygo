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
import com.taffygo.browser.ui.core.browser.SearchEngineRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-402's one source of truth. */
class GeneralViewModel(
    private val general: GeneralSettingsRepository,
    searchEngines: SearchEngineRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val picking = MutableStateFlow(false)
    private val pickingDownloadLocation = MutableStateFlow(false)
    private val savingDownloadLocation = MutableStateFlow(false)
    private val downloadLocationFailure = MutableStateFlow<DownloadLocationFailure?>(null)
    private val downloadSaveState = combine(
        savingDownloadLocation,
        downloadLocationFailure,
        ::DownloadSaveState,
    )

    val state: StateFlow<GeneralUiState> = combine(
        searchEngines.selectedId,
        picking,
        pickingDownloadLocation,
        downloadSaveState,
        general.snapshot,
    ) { selectedId, pickingSearchEngine, pickingLocation, saveState, settings ->
        GeneralUiState(
            searchEngineAvailable = settings.searchEngineAvailable,
            selectedEngineId = selectedId,
            pickingSearchEngine = pickingSearchEngine,
            pickingDownloadLocation = pickingLocation,
            downloadLocationsLoading = settings.downloadLocationsLoading,
            downloadLocationAvailable = settings.downloadLocationAvailable,
            downloadLocations = settings.downloadLocations.map {
                DownloadLocation(id = it.id, kind = it.kind)
            },
            selectedDownloadLocationId = settings.selectedDownloadLocationId,
            savingDownloadLocation = saveState.saving,
            downloadLocationFailure = saveState.failure ?: when {
                settings.downloadLocationSelectionFailed ->
                    DownloadLocationFailure.SELECTION_UNAVAILABLE
                settings.downloadLocationReadFailed -> DownloadLocationFailure.READBACK_FAILED
                else -> null
            },
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = GeneralUiState(
            searchEngineAvailable = general.snapshot.value.searchEngineAvailable,
            selectedEngineId = searchEngines.selectedId.value,
            pickingSearchEngine = picking.value,
            pickingDownloadLocation = pickingDownloadLocation.value,
            downloadLocationsLoading = general.snapshot.value.downloadLocationsLoading,
            downloadLocationAvailable = general.snapshot.value.downloadLocationAvailable,
            downloadLocations = general.snapshot.value.downloadLocations.map {
                DownloadLocation(id = it.id, kind = it.kind)
            },
            selectedDownloadLocationId = general.snapshot.value.selectedDownloadLocationId,
            downloadLocationFailure = when {
                general.snapshot.value.downloadLocationSelectionFailed ->
                    DownloadLocationFailure.SELECTION_UNAVAILABLE
                general.snapshot.value.downloadLocationReadFailed ->
                    DownloadLocationFailure.READBACK_FAILED
                else -> null
            },
        ),
    )

    fun onIntent(intent: GeneralIntent, navigator: TaffyNavigator) {
        val current = state.value
        val reduced = reduceGeneral(current, intent)
        picking.value = reduced.pickingSearchEngine
        pickingDownloadLocation.value = reduced.pickingDownloadLocation
        savingDownloadLocation.value = reduced.savingDownloadLocation
        downloadLocationFailure.value = reduced.downloadLocationFailure
        when (intent) {
            GeneralIntent.OpenAppearance -> navigator.goTo(TaffyDestination.Appearance)
            GeneralIntent.Dismiss -> navigator.goBack()
            GeneralIntent.ChooseDownloadLocation -> if (
                !current.pickingDownloadLocation && reduced.pickingDownloadLocation
            ) {
                general.refreshDownloadLocations()
            }
            is GeneralIntent.SelectDownloadLocation -> if (
                !current.savingDownloadLocation && reduced.savingDownloadLocation
            ) {
                viewModelScope.launch {
                    val result = general.chooseDownloadLocation(intent.id)
                    savingDownloadLocation.value = false
                    if (result == GeneralSettingsRepository.DownloadLocationChoice.SAVED) {
                        downloadLocationFailure.value = null
                        pickingDownloadLocation.value = false
                    } else {
                        downloadLocationFailure.value = result.toFailure()
                    }
                }
            }
            GeneralIntent.ChooseSearchEngine,
            GeneralIntent.DismissSearchEngine,
            GeneralIntent.DismissDownloadLocation,
            -> Unit
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.General.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }

    private data class DownloadSaveState(
        val saving: Boolean,
        val failure: DownloadLocationFailure?,
    )
}

private fun GeneralSettingsRepository.DownloadLocationChoice.toFailure(): DownloadLocationFailure =
    when (this) {
        GeneralSettingsRepository.DownloadLocationChoice.SAVED ->
            error("A saved download location has no failure")
        GeneralSettingsRepository.DownloadLocationChoice.SELECTION_UNAVAILABLE ->
            DownloadLocationFailure.SELECTION_UNAVAILABLE
        GeneralSettingsRepository.DownloadLocationChoice.WRITE_FAILED ->
            DownloadLocationFailure.WRITE_FAILED
        GeneralSettingsRepository.DownloadLocationChoice.READBACK_FAILED ->
            DownloadLocationFailure.READBACK_FAILED
    }
