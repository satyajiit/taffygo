// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-401 — identity doorway, a toolbar search CTA, and OpenAlly
 * labeled list groups. Never a grid: two-up tiles clipped the titles.
 */
@Composable
fun SettingsHomeScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    selected: SettingsSection? = null,
    onBack: () -> Unit = { navigator.goBack() },
) {
    val viewModel: SettingsHomeViewModel = screenViewModel(TaffyDestination.SettingsHome)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    SettingsHomeContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = onBack,
        selected = selected,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun SettingsHomeContent(
    state: SettingsHomeUiState,
    onIntent: (SettingsHomeIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    selected: SettingsSection? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.SettingsHome,
        title = taffyString(R.string.taffy_settings_title),
        onBack = onBack,
        toolbarRight = {
            SettingsSearchAction(
                open = state.searchOpen,
                onClick = { onIntent(SettingsHomeIntent.ToggleSearch) },
            )
        },
        header = if (state.searchOpen) {
            {
                TaffySearchField(
                    value = state.query,
                    onValueChange = { onIntent(SettingsHomeIntent.QueryChanged(it)) },
                    placeholder = taffyString(R.string.taffy_settings_search),
                    testTag = SETTINGS_SEARCH_TEST_TAG,
                )
            }
        } else {
            null
        },
        modifier = modifier,
    ) {
        if (!state.searching) {
            SettingsIdentityCard(
                displayName = state.displayName,
                avatar = state.avatar,
                monogram = state.monogram,
                onOpenYou = { onIntent(SettingsHomeIntent.Open(TaffyDestination.You)) },
            )
        }

        val searchText = state.sections.associateWith { settingsSearchText(it) }
        val titles = state.sections.associateWith { taffyString(it.titleRes) }
        val summaries = state.sections.associateWith { settingsRowSummary(it, state) }
        val hits = SettingsSearchIndex.hits(
            query = state.query,
            sections = state.sections,
            searchTextOf = { section -> searchText.getValue(section) },
            titleOf = { section -> titles.getValue(section) },
            summaryOf = { section -> summaries.getValue(section) },
        )

        if (hits.isEmpty()) {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_settings_no_matches_title),
                body = taffyString(R.string.taffy_settings_no_matches_body),
            )
        } else {
            SettingsHomeHits(
                hits = hits,
                selected = selected,
                grouped = !state.searching,
                onOpen = { onIntent(SettingsHomeIntent.Open(it)) },
            )
        }
    }
}

@Composable
private fun SettingsSearchAction(open: Boolean, onClick: () -> Unit) {
    TaffyPressable(
        onClick = onClick,
        testTag = SETTINGS_SEARCH_ACTION_TEST_TAG,
    ) {
        Box(
            modifier = Modifier.size(TaffyTheme.spacing.minimumTouchTarget),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = if (open) TaffyIcon.X else TaffyIcon.MagnifyingGlass,
                contentDescription = taffyString(
                    if (open) {
                        R.string.taffy_settings_search_close
                    } else {
                        R.string.taffy_settings_search
                    },
                ),
                tint = TaffyTheme.colors.textPrimary,
                modifier = Modifier.size(SearchGlyphSize),
            )
        }
    }
}

@Composable
private fun settingsSearchText(section: SettingsSection): List<String> = buildList {
    add(taffyString(section.titleRes))
    add(taffyString(section.summaryRes))
    SettingsSearchIndex.synonymRes(section)?.let { add(taffyString(it)) }
}

@Composable
private fun settingsRowSummary(section: SettingsSection, state: SettingsHomeUiState): String {
    val week = state.blockedThisWeek
    return if (section == SettingsSection.AD_AND_TRACKER_BLOCKING && week != null) {
        taffyPlural(
            R.plurals.taffy_settings_blocking_summary_week,
            week.coerceAtMost(Int.MAX_VALUE.toLong()).toInt(),
            week,
        )
    } else {
        taffyString(section.summaryRes)
    }
}

/** The tags screen SCR-401's semantics tests name. */
const val SETTINGS_SEARCH_TEST_TAG: String = "settings_search"
const val SETTINGS_SEARCH_ACTION_TEST_TAG: String = "settings_search_action"
const val SETTINGS_HEADER_TEST_TAG: String = "settings_header"
const val SETTINGS_LIST_TEST_TAG: String = "settings_list"
const val SETTINGS_GRID_TEST_TAG: String = "settings_grid"
const val SETTINGS_SECTION_TEST_TAG_PREFIX: String = "settings_section_"

private val SearchGlyphSize = 22.dp
