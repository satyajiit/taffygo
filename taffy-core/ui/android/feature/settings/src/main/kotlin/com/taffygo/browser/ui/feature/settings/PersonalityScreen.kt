// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
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
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-603 — How Taffy talks. */
@Composable
fun PersonalityScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: PersonalityViewModel = screenViewModel(TaffyDestination.Personality)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    PersonalityContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun PersonalityContent(
    state: PersonalityUiState,
    onIntent: (PersonalityIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.Personality,
        title = taffyString(R.string.taffy_personality_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        when (state.availability) {
            PersonalityRepository.Availability.LOADING -> TaffyHubSkeletonList(
                description = taffyString(R.string.taffy_personality_loading),
                testTag = PERSONALITY_LOADING_TEST_TAG,
            )
            PersonalityRepository.Availability.UNAVAILABLE,
            PersonalityRepository.Availability.READY,
            -> PersonalityBody(state, onIntent)
        }
    }
}

@Composable
private fun PersonalityBody(
    state: PersonalityUiState,
    onIntent: (PersonalityIntent) -> Unit,
) {
    if (state.availability == PersonalityRepository.Availability.UNAVAILABLE) {
        TaffyEmptyState(
            title = taffyString(R.string.taffy_personality_empty_title),
            body = taffyString(R.string.taffy_personality_empty_body),
            leading = {
                Icon(
                    imageVector = TaffyIcon.ChatCircle,
                    contentDescription = null,
                    tint = TaffyTheme.colors.textPrimary,
                    modifier = Modifier.size(SettingsGlyphSize),
                )
            },
        )
    } else {
        Text(
            text = taffyString(R.string.taffy_personality_intro),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(PERSONALITY_INTRO_TEST_TAG),
        )
    }
    if (state.choiceNotSaved) {
        Text(
            text = taffyString(R.string.taffy_personality_not_saved),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(PERSONALITY_NOT_SAVED_TEST_TAG),
        )
    }
    state.presets.forEach { preset ->
        PersonalityPresetCard(
            preset = preset,
            selected = preset == state.selected,
            onClick = { onIntent(PersonalityIntent.ChoosePreset(preset)) },
        )
    }
    TaffyGroupedCard {
        TaffyHubRow(
            title = taffyString(R.string.taffy_personality_tune),
            summary = taffyString(R.string.taffy_personality_tune_summary),
            icon = TaffyIcon.SlidersHorizontal,
            testTag = PERSONALITY_TUNE_ROW_TEST_TAG,
            onClick = { onIntent(PersonalityIntent.OpenTuning) },
        )
    }
}

@Composable
private fun PersonalityPresetCard(
    preset: PersonalityRepository.Preset,
    selected: Boolean,
    onClick: () -> Unit,
) {
    val title = taffyString(personalityPresetTitleRes(preset))
    val summary = taffyString(personalityPresetSummaryRes(preset))
    val description = taffyString(
        if (selected) {
            R.string.taffy_personality_preset_selected_description
        } else {
            R.string.taffy_personality_preset_description
        },
        title,
        summary,
    )
    TaffyGroupedCard {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(if (selected) TaffyTheme.colors.accentWash else TaffyTheme.colors.surfaceRaised)
                .clickable(onClick = onClick)
                .heightIn(min = PresetRowMinHeight)
                .padding(TaffyTheme.spacing.screenMargin)
                .testTag("$PERSONALITY_PRESET_TEST_TAG_PREFIX${preset.name}")
                .semantics(mergeDescendants = true) {
                    contentDescription = description
                    this.selected = selected
                    role = Role.Button
                },
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SettingsGlyph(personalityPresetIcon(preset))
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(
                    text = title,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = summary,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            if (selected) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(
                        imageVector = TaffyIcon.Check,
                        contentDescription = null,
                        tint = taffyHubAccentInk(),
                        modifier = Modifier.size(PresetGlyphSize),
                    )
                    Text(
                        text = taffyString(R.string.taffy_personality_selected),
                        style = TaffyTheme.typography.detail,
                        color = taffyHubAccentInk(),
                    )
                }
            }
        }
    }
}

/** The tags screen SCR-603's semantics tests name. */
const val PERSONALITY_LOADING_TEST_TAG: String = "personality_loading"
const val PERSONALITY_INTRO_TEST_TAG: String = "personality_intro"
const val PERSONALITY_NOT_SAVED_TEST_TAG: String = "personality_not_saved"
const val PERSONALITY_PRESET_TEST_TAG_PREFIX: String = "personality_preset_"
const val PERSONALITY_TUNE_ROW_TEST_TAG: String = "personality_tune"

private val PresetRowMinHeight = 72.dp
private val PresetGlyphSize = 20.dp
