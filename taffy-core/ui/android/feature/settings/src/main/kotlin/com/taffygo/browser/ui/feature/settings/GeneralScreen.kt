// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-402 — General. */
@Composable
fun GeneralScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: GeneralViewModel = screenViewModel(TaffyDestination.General)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    if (state.pickingSearchEngine) {
        SearchEngineScreen(
            onBack = { viewModel.onIntent(GeneralIntent.DismissSearchEngine, navigator) },
            modifier = modifier,
        )
    } else if (state.pickingDownloadLocation) {
        DownloadLocationContent(
            state = state,
            onIntent = { viewModel.onIntent(it, navigator) },
            onBack = { viewModel.onIntent(GeneralIntent.DismissDownloadLocation, navigator) },
            modifier = modifier,
        )
    } else {
        GeneralContent(
            state = state,
            onIntent = { viewModel.onIntent(it, navigator) },
            onBack = if (showUp) ({ navigator.goBack() }) else null,
            modifier = modifier,
        )
    }
}

/** The stateless half. */
@Composable
fun GeneralContent(
    state: GeneralUiState,
    onIntent: (GeneralIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val engineName = taffyString(searchEngineNameRes(state.selectedEngineId))
    val selectedLocation = state.downloadLocations.firstOrNull {
        it.id == state.selectedDownloadLocationId
    }
    val downloadSummary = when {
        state.downloadLocationsLoading -> taffyString(R.string.taffy_general_downloads_loading)
        selectedLocation != null -> downloadLocationName(selectedLocation.kind)
        else -> taffyString(R.string.taffy_general_downloads_unavailable)
    }
    TaffyScreen(
        destination = TaffyDestination.General,
        title = taffyString(R.string.taffy_general_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        Column(
            modifier = Modifier.testTag(GENERAL_LIST_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            SearchEngineDestination(
                engineName = engineName,
                markFile = state.selectedEngineId.markFile,
                enabled = state.searchEngineAvailable,
                onClick = { onIntent(GeneralIntent.ChooseSearchEngine) },
            )
            TaffyGroupedCard {
                SettingsHomeRow(
                    title = taffyString(R.string.taffy_general_downloads),
                    summary = downloadSummary,
                    icon = TaffyIcon.DownloadSimple,
                    testTag = GENERAL_DOWNLOADS_TEST_TAG,
                    selected = false,
                    accentSelected = false,
                    onClick = { onIntent(GeneralIntent.ChooseDownloadLocation) },
                    enabled = state.downloadLocationAvailable,
                )
                TaffyGroupedCardDivider()
                SettingsHomeRow(
                    title = taffyString(R.string.taffy_general_text_size),
                    summary = taffyString(R.string.taffy_general_text_size_note),
                    icon = TaffyIcon.TextAa,
                    testTag = GENERAL_TEXT_SIZE_TEST_TAG,
                    selected = false,
                    accentSelected = false,
                    onClick = { onIntent(GeneralIntent.OpenAppearance) },
                )
            }
            state.downloadLocationFailure?.let { DownloadLocationFailureTile(it) }
        }
    }
}

/** The live Chromium location picker remains part of SCR-402. */
@Composable
private fun DownloadLocationContent(
    state: GeneralUiState,
    onIntent: (GeneralIntent) -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    TaffyScreen(
        destination = TaffyDestination.General,
        title = taffyString(R.string.taffy_general_downloads_choose),
        onBack = onBack,
        modifier = modifier,
    ) {
        state.downloadLocationFailure?.let { DownloadLocationFailureTile(it) }
        if (state.downloadLocations.isEmpty()) {
            Text(
                text = if (state.downloadLocationsLoading) {
                    taffyString(R.string.taffy_general_downloads_loading)
                } else {
                    taffyString(R.string.taffy_general_downloads_unavailable)
                },
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        } else {
            TaffyGroupedCard(testTag = GENERAL_DOWNLOAD_LOCATION_LIST_TEST_TAG) {
                state.downloadLocations.forEachIndexed { index, location ->
                    DownloadLocationRow(
                        location = location,
                        selected = location.id == state.selectedDownloadLocationId,
                        enabled = !state.savingDownloadLocation,
                        onSelect = { onIntent(GeneralIntent.SelectDownloadLocation(location.id)) },
                    )
                    if (index < state.downloadLocations.lastIndex) TaffyGroupedCardDivider()
                }
            }
        }
    }
}

@Composable
private fun DownloadLocationFailureTile(failure: DownloadLocationFailure) {
    val message = when (failure) {
        DownloadLocationFailure.SELECTION_UNAVAILABLE ->
            R.string.taffy_general_downloads_selection_failed
        DownloadLocationFailure.WRITE_FAILED ->
            R.string.taffy_general_downloads_write_failed
        DownloadLocationFailure.READBACK_FAILED ->
            R.string.taffy_general_downloads_readback_failed
    }
    TaffyInfoTile(testTag = GENERAL_DOWNLOAD_LOCATION_FAILURE_TEST_TAG) {
        Text(
            text = taffyString(message),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

@Composable
private fun DownloadLocationRow(
    location: DownloadLocation,
    selected: Boolean,
    enabled: Boolean,
    onSelect: () -> Unit,
) {
    val name = downloadLocationName(location.kind)
    val description = if (selected) {
        taffyString(R.string.taffy_general_downloads_selected, name)
    } else {
        name
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(if (enabled) Modifier.clickable(onClick = onSelect) else Modifier)
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .semantics(mergeDescendants = true) {
                contentDescription = description
                this.selected = selected
                role = Role.RadioButton
            }
            .testTag("$GENERAL_DOWNLOAD_LOCATION_TEST_TAG_PREFIX${location.id}"),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.DownloadSimple,
            contentDescription = null,
            tint = TaffyTheme.colors.textPrimary,
            modifier = Modifier.size(20.dp),
        )
        Text(
            text = name,
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.weight(1f),
        )
        if (selected) {
            Icon(
                imageVector = TaffyIcon.Check,
                contentDescription = null,
                tint = TaffyTheme.colors.accentDeep,
                modifier = Modifier.size(20.dp),
            )
        }
    }
}

@Composable
private fun downloadLocationName(
    kind: GeneralSettingsRepository.DownloadLocation.Kind,
): String = taffyString(
    when (kind) {
        GeneralSettingsRepository.DownloadLocation.Kind.DEVICE ->
            R.string.taffy_general_downloads_device
        GeneralSettingsRepository.DownloadLocation.Kind.REMOVABLE_STORAGE ->
            R.string.taffy_general_downloads_removable
    },
)

@Composable
private fun SearchEngineDestination(
    engineName: String,
    markFile: String,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    val description = taffyString(R.string.taffy_general_search_engine_description, engineName)
    TaffyObjectCard(
        onClick = if (enabled) onClick else null,
        testTag = GENERAL_SEARCH_ENGINE_TEST_TAG,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                .semantics(mergeDescendants = true) {
                    contentDescription = description
                    role = Role.Button
                },
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SearchEngineMark(markFile = markFile)
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(
                    text = taffyString(R.string.taffy_general_search_engine),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
                Text(
                    text = if (enabled) {
                        engineName
                    } else {
                        taffyString(R.string.taffy_general_search_engine_unavailable)
                    },
                    style = if (enabled) {
                        TaffyTheme.typography.title
                    } else {
                        TaffyTheme.typography.detail
                    },
                    color = TaffyTheme.colors.textPrimary,
                )
            }
            Icon(
                imageVector = TaffyIcon.CaretRight,
                contentDescription = null,
                tint = TaffyTheme.colors.hairline,
                modifier = Modifier.size(ChevronSize),
            )
        }
    }
}

const val GENERAL_LIST_TEST_TAG: String = "general_list"
const val GENERAL_SEARCH_ENGINE_TEST_TAG: String = "general_search_engine"
const val GENERAL_DOWNLOADS_TEST_TAG: String = "general_downloads"
const val GENERAL_TEXT_SIZE_TEST_TAG: String = "general_text_size"
const val GENERAL_OPEN_APPEARANCE_TEST_TAG: String = "general_open_appearance"
const val GENERAL_DOWNLOAD_LOCATION_LIST_TEST_TAG: String = "general_download_location_list"
const val GENERAL_DOWNLOAD_LOCATION_TEST_TAG_PREFIX: String = "general_download_location_"
const val GENERAL_DOWNLOAD_LOCATION_FAILURE_TEST_TAG: String = "general_download_location_failure"
private val ChevronSize = 18.dp
