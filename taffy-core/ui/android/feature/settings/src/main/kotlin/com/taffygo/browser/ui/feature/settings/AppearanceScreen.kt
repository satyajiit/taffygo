// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySegmentedControl
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-407 — Appearance. */
@Composable
fun AppearanceScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: AppearanceViewModel = screenViewModel(TaffyDestination.Appearance)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    AppearanceContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun AppearanceContent(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.Appearance,
        title = taffyString(R.string.taffy_appearance_title),
        onBack = onBack,
        modifier = modifier,
        header = { AppearanceTabs(state = state, onIntent = onIntent) },
    ) {
        when (state.tab) {
            AppearanceTab.THEME -> AppearanceThemePane(state = state, onIntent = onIntent)
            AppearanceTab.LANGUAGE -> AppearanceLanguagePane(state = state, onIntent = onIntent)
        }
    }

    if (state.regionPickerVisible) {
        AppearanceCountryPicker(state = state, onIntent = onIntent)
    }
}

@Composable
private fun AppearanceTabs(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
) {
    TaffySegmentedControl(
        options = AppearanceTab.entries.map { taffyString(tabName(it)) },
        selectedIndex = state.tab.ordinal,
        onSelect = { index ->
            onIntent(AppearanceIntent.SelectTab(AppearanceTab.entries[index]))
        },
        modifier = Modifier
            .fillMaxWidth()
            .testTag(APPEARANCE_TABS_TEST_TAG),
        optionModifier = { index ->
            Modifier.testTag("$APPEARANCE_TAB_TEST_TAG_PREFIX${AppearanceTab.entries[index].name}")
        },
    )
}

@Composable
private fun AppearanceThemePane(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
) {
    AppearanceThemeSection(state = state, onIntent = onIntent)
    AppearanceForceDarkRow(state = state, onIntent = onIntent)
    SettingsHomeEyebrow(title = taffyString(R.string.taffy_appearance_text_size))
    TaffyGroupedCard {
        Text(
            text = taffyString(R.string.taffy_appearance_text_size_note),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier
                .padding(TaffyTheme.spacing.cardPadding)
                .testTag(TEXT_SIZE_NOTE_TEST_TAG),
        )
    }
}

@Composable
private fun AppearanceLanguagePane(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
) {
    AppearanceLanguageSection(state = state, onIntent = onIntent)
    AppearancePseudoRow(state = state, onIntent = onIntent)
}

@Composable
private fun AppearanceForceDarkRow(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
) {
    val name = taffyString(R.string.taffy_appearance_dark_sites_title)
    TaffyObjectCard(testTag = FORCE_DARK_ROW_TEST_TAG) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(
                    text = name,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = taffyString(R.string.taffy_appearance_dark_sites_body),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            TaffySwitch(
                checked = state.forceDarkWeb,
                onCheckedChange = { onIntent(AppearanceIntent.ToggleForceDarkWeb) },
                accessibleName = name,
                testTag = FORCE_DARK_SWITCH_TEST_TAG,
            )
        }
    }
}

@Composable
private fun AppearancePseudoRow(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
) {
    val name = taffyString(R.string.taffy_appearance_pseudo_title)
    TaffyObjectCard(testTag = PSEUDO_ROW_TEST_TAG) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(
                    text = name,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = taffyString(R.string.taffy_appearance_pseudo_body),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            TaffySwitch(
                checked = state.pseudoLocalization,
                onCheckedChange = { onIntent(AppearanceIntent.TogglePseudoLocalization) },
                accessibleName = name,
                testTag = PSEUDO_SWITCH_TEST_TAG,
            )
        }
    }
}

private fun tabName(tab: AppearanceTab) = when (tab) {
    AppearanceTab.THEME -> R.string.taffy_appearance_tab_theme
    AppearanceTab.LANGUAGE -> R.string.taffy_appearance_tab_language
}

/** The tags screen SCR-407's semantics tests name. */
const val APPEARANCE_TABS_TEST_TAG: String = "appearance_tabs"
const val APPEARANCE_TAB_TEST_TAG_PREFIX: String = "appearance_tab_"
const val THEME_LIST_TEST_TAG: String = "appearance_themes"
const val THEME_TEST_TAG_PREFIX: String = "appearance_theme_"
const val LANGUAGE_LIST_TEST_TAG: String = "appearance_languages"
const val LANGUAGE_TEST_TAG_PREFIX: String = "appearance_language_"
const val COUNTRY_PILL_TEST_TAG: String = "appearance_country"
const val COUNTRY_PICKER_TEST_TAG: String = "appearance_country_picker"
const val COUNTRY_SEARCH_TEST_TAG: String = "appearance_country_search"
const val COUNTRY_TEST_TAG_PREFIX: String = "appearance_region_country_"
const val TEXT_SIZE_NOTE_TEST_TAG: String = "appearance_text_size_note"
const val PSEUDO_ROW_TEST_TAG: String = "appearance_pseudo"
const val PSEUDO_SWITCH_TEST_TAG: String = "appearance_pseudo_switch"
const val FORCE_DARK_ROW_TEST_TAG: String = "appearance_force_dark"
const val FORCE_DARK_SWITCH_TEST_TAG: String = "appearance_force_dark_switch"
