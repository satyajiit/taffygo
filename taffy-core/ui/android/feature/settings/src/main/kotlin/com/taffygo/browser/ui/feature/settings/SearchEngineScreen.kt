// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.rememberScreenViewModelStoreOwner
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The search-engine list opened from General (SCR-402).
 *
 * It is not a catalog destination of its own: the catalog row is General.
 * The picker is shown in place of General's body so routing stays inside
 * this track.
 */
@Composable
fun SearchEngineScreen(
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val viewModel: SearchEngineViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(TaffyDestination.General),
        key = SEARCH_ENGINE_VIEW_MODEL_KEY,
    )
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    SearchEngineContent(
        state = state,
        onIntent = { intent ->
            viewModel.onIntent(intent)
            if (intent is SearchEngineIntent.Dismiss) onBack()
        },
        onBack = onBack,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun SearchEngineContent(
    state: SearchEngineUiState,
    onIntent: (SearchEngineIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.General,
        title = taffyString(R.string.taffy_search_engine_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        TaffyGroupedCard(testTag = SEARCH_ENGINE_LIST_TEST_TAG) {
            state.engines.forEachIndexed { index, choice ->
                SearchEngineChoiceRow(
                    choice = choice,
                    onSelect = { onIntent(SearchEngineIntent.Select(choice.id)) },
                )
                if (index < state.engines.lastIndex) TaffyGroupedCardDivider()
            }
        }
    }
}

const val SEARCH_ENGINE_LIST_TEST_TAG: String = "search_engine_list"
internal const val SEARCH_ENGINE_VIEW_MODEL_KEY: String = "search-engine"
