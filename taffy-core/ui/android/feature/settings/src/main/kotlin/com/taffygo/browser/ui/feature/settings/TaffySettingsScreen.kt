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
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-405 — Taffy settings hub. */
@Composable
fun TaffySettingsScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: TaffySettingsViewModel = screenViewModel(TaffyDestination.TaffySettings)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    TaffySettingsContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun TaffySettingsContent(
    state: TaffySettingsUiState,
    onIntent: (TaffySettingsIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.TaffySettings,
        title = taffyString(R.string.taffy_taffy_hub_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        TaffyHubDestinations(onIntent = onIntent)
        TaffyHubComposerSuggestions(state = state, onIntent = onIntent)
    }
}

/** The tags screen SCR-405's semantics tests name. */
const val TAFFY_HUB_LIST_TEST_TAG: String = "taffy_hub_list"
const val TAFFY_HUB_TALK_TEST_TAG: String = "taffy_hub_talk"
const val TAFFY_HUB_SKILLS_TEST_TAG: String = "taffy_hub_skills"
const val TAFFY_HUB_AI_TEST_TAG: String = "taffy_hub_ai"
const val TAFFY_HUB_SUGGESTIONS_TEST_TAG: String = "taffy_hub_suggestions"
const val TAFFY_HUB_SUGGESTIONS_BODY_TEST_TAG: String = "taffy_hub_suggestions_body"
const val TAFFY_HUB_SUGGESTIONS_SWITCH_TEST_TAG: String = "taffy_hub_suggestions_switch"
