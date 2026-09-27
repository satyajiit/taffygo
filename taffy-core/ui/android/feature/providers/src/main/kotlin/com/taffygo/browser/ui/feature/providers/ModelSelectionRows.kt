// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.selectable
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One model: what it is called, whose it is, what it can do, and — where it is
 * the one in force — the thinking beside it.
 *
 * A locked row is the same row with a different press. It is drawn as a button
 * that leads to the provider's own page rather than as one of a set of choices,
 * because a model behind a provider this browser cannot reach is not something
 * a person can pick yet, and a radio button that quietly navigated elsewhere
 * would be the wrong promise.
 */
@Composable
internal fun ModelSelectionRowItem(
    row: ModelSelectionUiState.Row,
    onIntent: (ModelSelectionIntent) -> Unit,
    onOpenProvider: () -> Unit,
) {
    val state = modelState(row)
    val body: @Composable () -> Unit = {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(
                    text = row.displayName,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                ModelFactRow(row = row)
            }
            if (state.isNotEmpty()) {
                Text(
                    text = state,
                    style = TaffyTheme.typography.caption,
                    color = if (row.selected) {
                        TaffyTheme.colors.positiveText
                    } else {
                        TaffyTheme.colors.textSecondary
                    },
                    modifier = Modifier.testTag("$MODEL_STATE_TEST_TAG_PREFIX${row.modelId}"),
                )
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(TaffyTheme.spacing.cardPadding)
            .testTag("$MODEL_ROW_TEST_TAG_PREFIX${row.modelId}"),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        // Everything drawn is announced, in the order it is drawn, because both
        // wrappers merge their descendants and neither replaces them with a
        // sentence of its own. A row's pills are the reason: their number
        // varies with what the catalog stated, and a hand-written description
        // beside them is a second list that goes stale the first time one is
        // added.
        if (row.locked) {
            TaffyPressable(
                onClick = onOpenProvider,
                modifier = Modifier.fillMaxWidth(),
                content = { body() },
            )
        } else {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .selectable(selected = row.selected, role = Role.RadioButton) {
                        onIntent(ModelSelectionIntent.ChooseModel(row))
                    },
            ) {
                body()
            }
            ThinkingLevelRow(
                choice = row.thinking,
                onChoose = { level -> onIntent(ModelSelectionIntent.ChooseThinking(row, level)) },
            )
        }
    }
}

/**
 * What this model can do, at a glance.
 *
 * Each fact is drawn only when the catalog stated it, so a row for an entry
 * that named none of them collapses to its name rather than to a line of empty
 * pills.
 */
@Composable
private fun ModelFactRow(row: ModelSelectionUiState.Row) {
    val context = contextWindowLabel(row.contextWindow)
    if (context == null && !row.reasoning && !row.readsPictures) return
    Row(
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (context != null) {
            ModelFactPill(
                label = context,
                testTag = "$MODEL_CONTEXT_TEST_TAG_PREFIX${row.modelId}",
            )
        }
        if (row.reasoning) {
            ModelFactPill(label = taffyString(R.string.taffy_providers_models_badge_thinks))
        }
        if (row.readsPictures) {
            ModelFactPill(label = taffyString(R.string.taffy_providers_models_badge_pictures))
        }
    }
}

/** One small fact about a model, in the shape every one of them uses. */
@Composable
private fun ModelFactPill(label: String, testTag: String? = null) {
    Text(
        text = label,
        style = TaffyTheme.typography.micro,
        color = TaffyTheme.colors.textSecondary,
        maxLines = 1,
        modifier = Modifier
            .clip(TaffyTheme.shapes.chip)
            .background(TaffyTheme.colors.surfaceSunken)
            .padding(
                horizontal = TaffyTheme.spacing.tight,
                vertical = TaffyTheme.spacing.step,
            )
            .then(if (testTag == null) Modifier else Modifier.testTag(testTag)),
    )
}

/**
 * How much this model can be given at once, or null when the catalog said
 * nothing.
 *
 * Zero is the catalog declining to say rather than a model that can be given
 * nothing, so it draws no pill — a pill reading zero would be a claim the
 * entry never made.
 */
@Composable
private fun contextWindowLabel(contextWindow: ULong): String? {
    if (contextWindow == 0uL) return null
    return taffyString(
        R.string.taffy_providers_models_context,
        taffyCount(contextWindow.toLong()),
    )
}

/**
 * The one word at the end of the row, and there are only three of them.
 *
 * In use is the roster's answer. Asking is this screen's own record that a
 * command went out — deliberately a different word, because a command that has
 * not been answered is not a choice that has taken. A locked row says what it
 * needs instead.
 */
@Composable
private fun modelState(row: ModelSelectionUiState.Row): String = when {
    row.locked -> taffyString(R.string.taffy_providers_models_needs_setup)
    row.selected -> taffyString(R.string.taffy_providers_models_in_use)
    row.asking -> taffyString(R.string.taffy_providers_models_asking)
    else -> ""
}

/** The name of the work a group of models is filed under. */
internal fun capabilityTitle(capability: ModelCapability): Int = when (capability) {
    ModelCapability.REASONING -> R.string.taffy_providers_models_group_reasoning
    ModelCapability.MULTIMODAL -> R.string.taffy_providers_models_group_pictures
    ModelCapability.STANDARD -> R.string.taffy_providers_models_group_plain
}

/** The tags screen SCR-417's semantics tests name. */
const val MODEL_ROW_TEST_TAG_PREFIX: String = "model_row_"
const val MODEL_STATE_TEST_TAG_PREFIX: String = "model_state_"
const val MODEL_CONTEXT_TEST_TAG_PREFIX: String = "model_context_"
