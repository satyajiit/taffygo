// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.SearchEngine
import com.taffygo.browser.ui.core.browser.SearchEngineCatalog
import com.taffygo.browser.ui.core.browser.SearchEngineId

internal fun reduceSearchEngine(
    state: SearchEngineUiState,
    intent: SearchEngineIntent,
): SearchEngineUiState = when (intent) {
    is SearchEngineIntent.Select -> state.copy(
        selectedId = intent.id,
        engines = state.engines.map { choice ->
            choice.copy(selected = choice.id == intent.id)
        },
    )
    SearchEngineIntent.Dismiss -> state
}

internal fun projectSearchEngine(
    selectedId: SearchEngineId,
    regionCode: String,
): SearchEngineUiState = SearchEngineUiState(
    selectedId = selectedId,
    engines = SearchEngineCatalog.listed(regionCode, selectedId).map { engine ->
        engine.toChoice(selectedId)
    },
)

private fun SearchEngine.toChoice(selectedId: SearchEngineId): SearchEngineUiState.Choice =
    SearchEngineUiState.Choice(
        id = id,
        markFile = id.markFile,
        selected = id == selectedId,
    )
