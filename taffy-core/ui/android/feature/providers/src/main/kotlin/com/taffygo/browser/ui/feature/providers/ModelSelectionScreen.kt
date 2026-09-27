// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-417 — the models on offer, with the thinking beside the one in
 * force.
 *
 * One screen for both shapes of the destination. The catalog names each model's
 * own provider, so a list narrowed to one provider and the whole list differ by
 * a filter rather than by being two surfaces; what changes with the shape is
 * only which question the headings answer.
 *
 * Nothing here is optimistic. A press states the whole choice through the one
 * command that carries it, and what the screen draws afterwards is the roster
 * the core publishes next — so a preference the core refuses never appears as
 * though it had taken.
 */
@Composable
fun ModelSelectionScreen(
    destination: TaffyDestination.ModelSelection,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: ModelSelectionViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    ModelSelectionContent(
        state = state,
        onIntent = { intent -> viewModel.onIntent(intent, navigator) },
        destination = destination,
        modifier = modifier,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
    )
}

/** The stateless half. */
@Composable
fun ModelSelectionContent(
    state: ModelSelectionUiState,
    onIntent: (ModelSelectionIntent) -> Unit,
    modifier: Modifier = Modifier,
    destination: TaffyDestination.ModelSelection = TaffyDestination.ModelSelection(),
    onBack: (() -> Unit)? = null,
) {
    val wholeCatalog = state.providerId == null
    TaffyLazyScreen(
        destination = destination,
        title = taffyString(
            if (wholeCatalog) {
                R.string.taffy_providers_models_all_title
            } else {
                R.string.taffy_providers_models_one_title
            },
        ),
        modifier = modifier,
        onBack = onBack,
        listModifier = if (state.status == ModelSelectionUiState.Status.READY) {
            Modifier.testTag(MODEL_LIST_TEST_TAG)
        } else {
            Modifier
        },
    ) {
        if (state.searchable) {
            item(key = "search", contentType = "search") {
                TaffySearchField(
                    value = state.query,
                    onValueChange = { typed -> onIntent(ModelSelectionIntent.Search(typed)) },
                    placeholder = taffyString(R.string.taffy_providers_models_search),
                    testTag = MODEL_SEARCH_TEST_TAG,
                )
            }
        }
        when (state.status) {
            ModelSelectionUiState.Status.LOADING -> item(contentType = "loading") {
                Text(
                    text = taffyString(R.string.taffy_providers_models_loading),
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.testTag(MODEL_LOADING_TEST_TAG),
                )
            }

            ModelSelectionUiState.Status.UNKNOWN -> item(contentType = "unknown") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_providers_config_gone_title),
                    body = taffyString(R.string.taffy_providers_config_gone_body),
                )
            }

            // Three absences, three sentences. Nothing published yet, a catalog
            // that named no model, and a search that excluded every one of them
            // have different causes and different next steps, and one shared
            // "no models" would send somebody looking for a fault twice over.
            ModelSelectionUiState.Status.EMPTY -> item(contentType = "empty") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_providers_models_empty_title),
                    body = taffyString(R.string.taffy_providers_models_empty_body),
                )
            }

            ModelSelectionUiState.Status.NO_MATCH -> item(contentType = "no-match") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_providers_models_no_match_title),
                    body = taffyString(R.string.taffy_providers_models_no_match_body),
                )
            }

            ModelSelectionUiState.Status.READY -> modelSelectionItems(
                state = state,
                wholeCatalog = wholeCatalog,
                onIntent = onIntent,
            )
        }
    }
}

/**
 * The two halves of the list, and the headings that only exist when it has two
 * halves.
 *
 * A page whose providers are all reachable is one list and says so by having no
 * section headings at all; the split appears the moment there is something a
 * person cannot use yet. Narrowed to a single provider there is nothing to
 * divide, so the models are drawn under the title and the reason a locked
 * provider cannot be used sits on the block itself.
 */
private fun LazyListScope.modelSelectionItems(
    state: ModelSelectionUiState,
    wholeCatalog: Boolean,
    onIntent: (ModelSelectionIntent) -> Unit,
) {
    val divided = wholeCatalog && state.locked.isNotEmpty()
    if (divided && state.ready.isNotEmpty()) {
        item(key = "ready-heading", contentType = "section-heading") {
            TaffySectionHeader(title = taffyString(R.string.taffy_providers_models_ready_title))
        }
    }
    state.ready.forEach { block ->
        modelProviderBlockItems(
            block = block,
            showProviderName = wholeCatalog,
            onIntent = onIntent,
        )
    }
    if (divided) {
        item(key = "locked-heading", contentType = "section-heading") {
            ModelLockedHeader(
                providerCount = state.locked.size,
                expanded = state.lockedExpanded,
                onToggle = { onIntent(ModelSelectionIntent.ToggleLocked) },
            )
        }
    }
    if (!divided || state.lockedExpanded) {
        state.locked.forEach { block ->
            modelProviderBlockItems(
                block = block,
                showProviderName = wholeCatalog,
                onIntent = onIntent,
            )
        }
    }
}

/** The tags screen SCR-417's wait state and search field carry. */
const val MODEL_LIST_TEST_TAG: String = "model_selection_list"
const val MODEL_LOADING_TEST_TAG: String = "model_selection_loading"
const val MODEL_SEARCH_TEST_TAG: String = "model_selection_search"
