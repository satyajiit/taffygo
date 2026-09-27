// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.StateFlow

/**
 * Chromium's writable download locations. Search engines are listed by
 * [com.taffygo.browser.ui.core.browser.SearchEngineRepository].
 */
interface GeneralSettingsRepository {
    val snapshot: StateFlow<Snapshot>

    /** Re-read the locations Chromium can actually write to on this phone. */
    fun refreshDownloadLocations()

    /** Persist one opaque location, with a truthful failure for every boundary. */
    suspend fun chooseDownloadLocation(id: String): DownloadLocationChoice

    data class Snapshot(
        val searchEngineAvailable: Boolean = true,
        val downloadLocationsLoading: Boolean = true,
        val downloadLocations: List<DownloadLocation> = emptyList(),
        val selectedDownloadLocationId: String? = null,
        val downloadLocationSelectionFailed: Boolean = false,
        val downloadLocationReadFailed: Boolean = false,
    ) {
        val downloadLocationAvailable: Boolean
            get() = !downloadLocationsLoading && downloadLocations.isNotEmpty()
    }

    /** A display-safe location. The filesystem path never crosses this interface. */
    data class DownloadLocation(
        val id: String,
        val kind: Kind,
    ) {
        enum class Kind { DEVICE, REMOVABLE_STORAGE }
    }

    enum class DownloadLocationChoice {
        SAVED,
        SELECTION_UNAVAILABLE,
        WRITE_FAILED,
        READBACK_FAILED,
    }
}

internal class UnavailableGeneralSettingsRepository : GeneralSettingsRepository {
    override val snapshot: StateFlow<GeneralSettingsRepository.Snapshot> =
        kotlinx.coroutines.flow.MutableStateFlow(
            GeneralSettingsRepository.Snapshot(
                searchEngineAvailable = false,
                downloadLocationsLoading = false,
            ),
        )

    override fun refreshDownloadLocations() = Unit

    override suspend fun chooseDownloadLocation(id: String) =
        GeneralSettingsRepository.DownloadLocationChoice.SELECTION_UNAVAILABLE
}

internal class EmptyGeneralSettingsRepository : GeneralSettingsRepository {
    override val snapshot: StateFlow<GeneralSettingsRepository.Snapshot> =
        kotlinx.coroutines.flow.MutableStateFlow(
            GeneralSettingsRepository.Snapshot(
                searchEngineAvailable = false,
                downloadLocationsLoading = false,
            ),
        )

    override fun refreshDownloadLocations() = Unit

    override suspend fun chooseDownloadLocation(id: String) =
        GeneralSettingsRepository.DownloadLocationChoice.SELECTION_UNAVAILABLE
}
