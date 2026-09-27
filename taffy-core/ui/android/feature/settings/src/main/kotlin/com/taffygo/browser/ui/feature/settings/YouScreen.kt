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

/** Screen SCR-410 — You. */
@Composable
fun YouScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
    onBack: () -> Unit = { navigator.goBack() },
) {
    val viewModel: YouViewModel = screenViewModel(TaffyDestination.You)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    YouContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) onBack else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun YouContent(
    state: YouUiState,
    onIntent: (YouIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val closeDetails = { onIntent(YouIntent.CloseDetails) }
    TaffyScreen(
        destination = TaffyDestination.You,
        title = taffyString(
            if (state.detailsOpen) {
                R.string.taffy_you_profile_title
            } else {
                R.string.taffy_settings_you_title
            },
        ),
        onBack = when {
            state.detailsOpen -> closeDetails
            else -> onBack
        },
        modifier = modifier,
    ) {
        if (state.detailsOpen) {
            YouDetails(state = state, onIntent = onIntent)
        } else {
            YouHero(
                state = state,
                onClick = { onIntent(YouIntent.OpenDetails) },
            )
            YouChapters(state = state, onOpen = { onIntent(YouIntent.Open(it)) })
            YouMoreRows(state = state, onOpen = { onIntent(YouIntent.Open(it)) })
            YouAdsNote()
        }
    }
}
