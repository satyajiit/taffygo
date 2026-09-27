// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-604 — Personality scales. */
@Composable
fun PersonalityTuningScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: PersonalityTuningViewModel =
        screenViewModel(TaffyDestination.PersonalityTuning)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    PersonalityTuningContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** One card per axis. A scale never changes what Taffy may do. */
@Composable
fun PersonalityTuningContent(
    state: PersonalityTuningUiState,
    onIntent: (PersonalityTuningIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.PersonalityTuning,
        title = taffyString(R.string.taffy_tuning_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        if (state.availability == PersonalityRepository.Availability.LOADING) {
            TaffyHubSkeletonList(
                description = taffyString(R.string.taffy_tuning_loading),
                testTag = TUNING_LOADING_TEST_TAG,
            )
            return@TaffyScreen
        }
        TaffyInfoTile(testTag = TUNING_CAPTION_TEST_TAG) {
            Text(
                text = taffyString(R.string.taffy_tuning_caption),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        if (state.notSaved ||
            state.availability == PersonalityRepository.Availability.UNAVAILABLE
        ) {
            TaffyInfoTile(testTag = TUNING_NOT_SAVED_TEST_TAG) {
                Text(
                    text = taffyString(R.string.taffy_personality_not_saved),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
        TuningAxis(
            title = taffyString(R.string.taffy_tuning_pace),
            options = listOf(
                taffyString(R.string.taffy_tuning_pace_careful),
                taffyString(R.string.taffy_tuning_mid),
                taffyString(R.string.taffy_tuning_pace_quick),
            ),
            selectedIndex = state.scales.pace,
            testTag = TUNING_PACE_TEST_TAG,
            optionPrefix = TUNING_PACE_OPTION_TEST_TAG_PREFIX,
            onSelect = { index ->
                onIntent(
                    PersonalityTuningIntent.SetScale(PersonalityTuningIntent.Axis.PACE, index),
                )
            },
        )
        TuningAxis(
            title = taffyString(R.string.taffy_tuning_length),
            options = listOf(
                taffyString(R.string.taffy_tuning_length_concise),
                taffyString(R.string.taffy_tuning_mid),
                taffyString(R.string.taffy_tuning_length_chatty),
            ),
            selectedIndex = state.scales.length,
            testTag = TUNING_LENGTH_TEST_TAG,
            optionPrefix = TUNING_LENGTH_OPTION_TEST_TAG_PREFIX,
            onSelect = { index ->
                onIntent(
                    PersonalityTuningIntent.SetScale(PersonalityTuningIntent.Axis.LENGTH, index),
                )
            },
        )
        TuningAxis(
            title = taffyString(R.string.taffy_tuning_checkin),
            options = listOf(
                taffyString(R.string.taffy_tuning_checkin_asks),
                taffyString(R.string.taffy_tuning_mid),
                taffyString(R.string.taffy_tuning_checkin_acts),
            ),
            selectedIndex = state.scales.checkIn,
            testTag = TUNING_CHECKIN_TEST_TAG,
            optionPrefix = TUNING_CHECKIN_OPTION_TEST_TAG_PREFIX,
            onSelect = { index ->
                onIntent(
                    PersonalityTuningIntent.SetScale(
                        PersonalityTuningIntent.Axis.CHECK_IN,
                        index,
                    ),
                )
            },
        )
    }
}

/** The tags screen SCR-604's semantics tests name. */
const val TUNING_LOADING_TEST_TAG: String = "tuning_loading"
const val TUNING_CAPTION_TEST_TAG: String = "tuning_caption"
const val TUNING_NOT_SAVED_TEST_TAG: String = "tuning_not_saved"
const val TUNING_PACE_TEST_TAG: String = "tuning_pace"
const val TUNING_LENGTH_TEST_TAG: String = "tuning_length"
const val TUNING_CHECKIN_TEST_TAG: String = "tuning_checkin"
const val TUNING_PACE_OPTION_TEST_TAG_PREFIX: String = "tuning_pace_"
const val TUNING_LENGTH_OPTION_TEST_TAG_PREFIX: String = "tuning_length_"
const val TUNING_CHECKIN_OPTION_TEST_TAG_PREFIX: String = "tuning_checkin_"
