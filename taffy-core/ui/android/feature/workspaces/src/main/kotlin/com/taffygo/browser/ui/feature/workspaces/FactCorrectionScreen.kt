// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.ImeAction
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyKeyValueRow
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-307 — the fact correction sheet.
 *
 * It shows what the page said, takes what the user says, and states plainly
 * that the two are kept side by side and that dependent cells will be
 * recomputed.
 */
@Composable
fun FactCorrectionScreen(
    destination: TaffyDestination.FactCorrection,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: FactCorrectionViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    FactCorrectionContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        destination = destination,
        showUp = showUp,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun FactCorrectionContent(
    state: FactCorrectionUiState,
    onIntent: (FactCorrectionIntent) -> Unit,
    modifier: Modifier = Modifier,
    destination: TaffyDestination.FactCorrection = TaffyDestination.FactCorrection("", ""),
    showUp: Boolean = true,
) {
    TaffyScreen(
        destination = destination,
        title = taffyString(R.string.taffy_fact_correction_title),
        onBack = if (showUp) ({ onIntent(FactCorrectionIntent.Cancel) }) else null,
        modifier = modifier,
    ) {
        when {
            state.loading -> WorkspaceSkeletonList(
                loadingDescription = taffyString(R.string.taffy_fact_correction_loading),
                testTag = FACT_LOADING_TEST_TAG,
            )
            state.unavailable -> TaffyEmptyState(
                title = taffyString(R.string.taffy_workspace_unavailable_title),
                body = taffyString(R.string.taffy_workspace_unavailable_body),
                leading = { WorkspaceEmptyGlyph(TaffyIcon.PencilSimple) },
            )
            state.missing -> TaffyEmptyState(
                title = taffyString(R.string.taffy_fact_correction_missing_title),
                body = taffyString(R.string.taffy_fact_correction_missing_body),
                leading = { WorkspaceEmptyGlyph(TaffyIcon.PencilSimple) },
            )
            else -> FactCorrectionForm(state = state, onIntent = onIntent)
        }
    }
}

@Composable
private fun FactCorrectionForm(
    state: FactCorrectionUiState,
    onIntent: (FactCorrectionIntent) -> Unit,
) {
    TaffyGroupedCard {
        FactCorrectionPair(
            label = taffyString(R.string.taffy_fact_correction_field),
            value = state.field,
        )
        WorkspaceCardHairline()
        FactCorrectionPair(
            label = taffyString(R.string.taffy_fact_correction_page_value),
            value = state.pageValue,
        )
    }

    // Done, and it saves. This is the last field on the screen and Save is
    // the affordance beside it, so the key sends the same intent the button
    // sends rather than a second route to the same place — and it sends it
    // under the same condition, because a key that could save what the
    // button refuses to save would be exactly that second route. When there
    // is nothing to save the key falls back to the platform's own handling
    // of Done, which puts the keyboard away.
    val keyboard = LocalSoftwareKeyboardController.current
    OutlinedTextField(
        value = state.enteredValue,
        onValueChange = { onIntent(FactCorrectionIntent.ValueChanged(it)) },
        label = { Text(text = taffyString(R.string.taffy_fact_correction_your_value)) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
        keyboardActions = KeyboardActions(
            onDone = {
                if (state.canSave) {
                    onIntent(FactCorrectionIntent.Save)
                } else {
                    keyboard?.hide()
                }
            },
        ),
        shape = TaffyTheme.shapes.row,
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = TaffyTheme.colors.surfaceSunken,
            unfocusedContainerColor = TaffyTheme.colors.surfaceSunken,
            focusedBorderColor = TaffyTheme.colors.focusRing,
            unfocusedBorderColor = TaffyTheme.colors.outline,
            cursorColor = TaffyTheme.colors.textPrimary,
        ),
        modifier = Modifier
            .fillMaxWidth()
            .testTag(VALUE_TEST_TAG),
    )

    Text(
        text = taffyString(
            R.string.taffy_fact_correction_notice,
            taffyPlural(
                R.plurals.taffy_fact_correction_downstream,
                state.downstreamCount,
                state.downstreamCount,
            ),
        ),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier.testTag(NOTICE_TEST_TAG),
    )

    Row(horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        TaffyPrimaryButton(
            label = taffyString(R.string.taffy_fact_correction_save),
            onClick = { onIntent(FactCorrectionIntent.Save) },
            enabled = state.canSave,
            testTag = SAVE_TEST_TAG,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_fact_correction_cancel),
            onClick = { onIntent(FactCorrectionIntent.Cancel) },
            testTag = CANCEL_TEST_TAG,
        )
    }
}

@Composable
private fun FactCorrectionPair(label: String, value: String) {
    TaffyKeyValueRow(
        label = label,
        value = value,
        modifier = Modifier
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            ),
    )
}

/** The tags screen SCR-307's semantics tests name. */
const val VALUE_TEST_TAG: String = "fact_correction_value"
const val NOTICE_TEST_TAG: String = "fact_correction_notice"
const val SAVE_TEST_TAG: String = "fact_correction_save"
const val CANCEL_TEST_TAG: String = "fact_correction_cancel"
const val FACT_LOADING_TEST_TAG: String = "fact_correction_loading"
