// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-402: open the engine list when one can be set; leave downloads alone. */
internal fun reduceGeneral(state: GeneralUiState, intent: GeneralIntent): GeneralUiState =
    when (intent) {
        GeneralIntent.ChooseSearchEngine ->
            if (state.searchEngineAvailable) state.copy(pickingSearchEngine = true) else state
        GeneralIntent.DismissSearchEngine -> state.copy(pickingSearchEngine = false)
        GeneralIntent.ChooseDownloadLocation ->
            if (state.downloadLocationAvailable) {
                state.copy(
                    pickingDownloadLocation = true,
                    downloadLocationFailure = null,
                )
            } else {
                state
            }
        GeneralIntent.DismissDownloadLocation -> state.copy(
            pickingDownloadLocation = false,
            downloadLocationFailure = null,
        )
        is GeneralIntent.SelectDownloadLocation ->
            if (
                state.downloadLocationAvailable &&
                !state.savingDownloadLocation &&
                state.downloadLocations.any { it.id == intent.id }
            ) {
                state.copy(
                    savingDownloadLocation = true,
                    downloadLocationFailure = null,
                )
            } else if (state.downloadLocationAvailable && !state.savingDownloadLocation) {
                state.copy(downloadLocationFailure = DownloadLocationFailure.SELECTION_UNAVAILABLE)
            } else {
                state
            }
        GeneralIntent.OpenAppearance,
        GeneralIntent.Dismiss,
        -> state
    }
