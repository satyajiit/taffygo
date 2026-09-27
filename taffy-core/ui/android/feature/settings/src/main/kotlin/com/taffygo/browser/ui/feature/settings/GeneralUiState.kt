// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.SearchEngineId

/** Screen SCR-402 — search engine, files, and the text-size note. */
data class GeneralUiState(
    val searchEngineAvailable: Boolean = true,
    val selectedEngineId: SearchEngineId = SearchEngineId.DEFAULT,
    val pickingSearchEngine: Boolean = false,
    val pickingDownloadLocation: Boolean = false,
    val downloadLocationsLoading: Boolean = true,
    val downloadLocationAvailable: Boolean = false,
    val downloadLocations: List<DownloadLocation> = emptyList(),
    val selectedDownloadLocationId: String? = null,
    val savingDownloadLocation: Boolean = false,
    val downloadLocationFailure: DownloadLocationFailure? = null,
)
