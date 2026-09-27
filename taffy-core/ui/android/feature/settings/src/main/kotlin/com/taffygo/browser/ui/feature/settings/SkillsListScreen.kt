// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-601 — What Taffy can do. */
@Composable
fun SkillsListScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: SkillsListViewModel = screenViewModel(TaffyDestination.SkillsList)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    SkillsListContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun SkillsListContent(
    state: SkillsListUiState,
    onIntent: (SkillsListIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val matching = matchingSkills(state)
    TaffyLazyScreen(
        destination = TaffyDestination.SkillsList,
        title = taffyString(R.string.taffy_skills_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = Modifier.testTag(SKILLS_LIST_TEST_TAG),
    ) {
        when (state.availability) {
            SkillsRepository.Availability.LOADING -> item(
                key = "skills-loading",
                contentType = "status",
            ) {
                TaffyHubSkeletonList(
                    description = taffyString(R.string.taffy_skills_loading),
                    testTag = SKILLS_LOADING_TEST_TAG,
                )
            }
            SkillsRepository.Availability.UNAVAILABLE -> item(
                key = "skills-unavailable",
                contentType = "status",
            ) {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_skills_unavailable_title),
                    body = taffyString(R.string.taffy_skills_unavailable_body),
                    leading = { SkillsEmptyGlyph() },
                )
            }
            SkillsRepository.Availability.READY -> skillsListReady(
                state = state,
                matching = matching,
                onIntent = onIntent,
            )
        }
    }
}

private fun LazyListScope.skillsListReady(
    state: SkillsListUiState,
    matching: List<SkillsListUiState.Group>,
    onIntent: (SkillsListIntent) -> Unit,
) {
    item(key = "skills-intro", contentType = "intro") {
        Text(
            text = taffyString(R.string.taffy_skills_intro),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(SKILLS_INTRO_TEST_TAG),
        )
    }
    if (state.showSearch) {
        item(key = "skills-search", contentType = "search") {
            TaffySearchField(
                value = state.query,
                onValueChange = { onIntent(SkillsListIntent.QueryChanged(it)) },
                placeholder = taffyString(R.string.taffy_skills_search),
                testTag = SKILLS_SEARCH_TEST_TAG,
            )
        }
    }
    if (!state.siteSkillsAvailable) {
        item(key = "site-skills-unavailable", contentType = "status") {
            Text(
                text = taffyString(R.string.taffy_skills_added_unavailable),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
    if (matching.isEmpty()) {
        item(key = "skills-no-matches", contentType = "status") {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_settings_no_matches_title),
                body = taffyString(R.string.taffy_settings_no_matches_body),
                leading = { SkillsEmptyGlyph() },
            )
        }
    } else {
        matching.forEach { group -> skillsListGroup(group, onIntent) }
    }
    if (state.showNoExtras && state.query.isBlank()) {
        item(key = "skills-no-extras", contentType = "status") {
            Text(
                text = taffyString(R.string.taffy_skills_no_extras),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(SKILLS_NO_EXTRAS_TEST_TAG),
            )
        }
    }
}

/**
 * Resolve localized search copy only when the skill roster or locale changes.
 * Typing then filters the cached strings instead of rebuilding every title,
 * summary, origin, and group label on each recomposition.
 */
@Composable
private fun matchingSkills(state: SkillsListUiState): List<SkillsListUiState.Group> {
    if (state.availability != SkillsRepository.Availability.READY) return emptyList()
    val resources = LocalResources.current
    val localeKey = LocalConfiguration.current.locales.toLanguageTags()
    val searchText = remember(state.groups, localeKey) {
        state.groups.flatMap { group ->
            val groupTitle = resources.getString(skillGroupTitleRes(group.group))
            group.skills.map { row ->
                row.id to listOfNotNull(
                    skillTitleRes(row.id)?.let { resources.getString(it) },
                    skillSummaryRes(row.id)?.let { resources.getString(it) },
                    row.name,
                    row.origin,
                    groupTitle,
                )
            }
        }.toMap()
    }
    return remember(state.groups, state.query, searchText) {
        state.matching { id -> searchText[id].orEmpty() }
    }
}

/** The tags screen SCR-601's semantics tests name. */
const val SKILLS_INTRO_TEST_TAG: String = "skills_intro"
const val SKILLS_SEARCH_TEST_TAG: String = "skills_search"
const val SKILLS_LOADING_TEST_TAG: String = "skills_loading"
const val SKILLS_LIST_TEST_TAG: String = "skills_list"
const val SKILLS_NO_EXTRAS_TEST_TAG: String = "skills_no_extras"
const val SKILLS_GROUP_TEST_TAG_PREFIX: String = "skills_group_"
const val SKILL_ROW_TEST_TAG_PREFIX: String = "skill_row_"
const val SKILL_SWITCH_TEST_TAG_PREFIX: String = "skill_switch_"

@Composable
private fun SkillsEmptyGlyph() {
    Icon(
        imageVector = TaffyIcon.PuzzlePiece,
        contentDescription = null,
        tint = TaffyTheme.colors.textPrimary,
        modifier = Modifier.size(SettingsGlyphSize),
    )
}
