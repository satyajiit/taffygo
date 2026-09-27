// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyString
import taffy.core_api.MAX_USER_INPUT_ANSWER_BYTES

/**
 * One card on the page for the one wait a task has open (decision 0132
 * section 4).
 *
 * A task that needs the person says so where they are — above the action
 * row of the page Taffy stopped on — and not on a screen they would have to
 * find. The one-door rule of `TaskProjection.wait` is what this draws: a
 * hand-over first, then field values, then an answer; never two at once.
 *
 * - A **hand-over** names the site and what to finish, and its primary
 *   action reads **Hand back to Taffy**. "Take over" keeps its meaning — the
 *   person taking the page from Taffy — and the two verbs name the two
 *   directions.
 * - **Field values** are the field-value sheet of decision 0088, which the
 *   shell passes in as [fieldValues] because it owns its own view model.
 * - An **answer** is the ask that used to live on the task view: the
 *   question in the core's words, a bounded field, and a send that refuses
 *   a code-shaped answer.
 *
 * It reads the same view model as the Assistant pill, so the card and the
 * pill's line can never disagree about which wait is open.
 */
@Composable
fun TaskWaitCard(
    navigator: TaffyNavigator,
    destination: TaffyDestination,
    modifier: Modifier = Modifier,
) {
    val viewModel: AssistantBarViewModel = screenViewModel(WaitFrame)
    val state by viewModel.state.collectAsStateWithLifecycle()
    TaskWaitCardContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        fieldValues = { TaskInputSheet(destination = destination) },
        modifier = modifier,
    )
}

/** The stateless half, which a preview and a semantics test render. */
@Composable
fun TaskWaitCardContent(
    state: AssistantBarUiState,
    onIntent: (AssistantBarIntent) -> Unit,
    modifier: Modifier = Modifier,
    fieldValues: @Composable () -> Unit = {},
) {
    if (state.hasHandover) {
        HandoverCard(state = state, onIntent = onIntent, modifier = modifier)
        return
    }
    // The sheet decides its own visibility from the request it holds; the
    // ask stands down while a form is open, which is the door order.
    fieldValues()
    if (state.hasAsk && !state.hasInputRequest) {
        AskCard(prompt = state.askPrompt, onIntent = onIntent, modifier = modifier)
    }
}

@Composable
private fun HandoverCard(
    state: AssistantBarUiState,
    onIntent: (AssistantBarIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val step = state.latestStep
    val host = step?.host?.takeIf { it.isNotBlank() }
    val title = if (host != null) {
        taffyString(R.string.taffy_task_wait_handover_title, host)
    } else {
        taffyString(R.string.taffy_task_wait_handover_title_here)
    }
    val body = taffyString(R.string.taffy_task_wait_finish)
    WaitCard(spoken = "$title. $body", testTag = HANDOVER_TEST_TAG, modifier = modifier) {
        Text(
            text = title,
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textPrimary,
        )
        Text(
            text = body,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug)) {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_task_wait_hand_back),
                onClick = { onIntent(AssistantBarIntent.CompleteHandover) },
                size = TaffyButtonSize.COMPACT,
                testTag = HANDOVER_DONE_TEST_TAG,
            )
            if (TaskControl.STOP in state.controls) {
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_task_wait_stop),
                    onClick = { onIntent(AssistantBarIntent.Control(TaskControl.STOP)) },
                    size = TaffyButtonSize.COMPACT,
                    testTag = HANDOVER_STOP_TEST_TAG,
                )
            }
        }
    }
}

@Composable
private fun AskCard(
    prompt: String?,
    onIntent: (AssistantBarIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    var answer by remember { mutableStateOf("") }
    val credentialShaped = isCredentialShapedAnswer(answer)
    val question = prompt?.takeIf { it.isNotEmpty() }
        ?: taffyString(R.string.taffy_task_view_ask_body)
    WaitCard(spoken = question, testTag = ASK_CARD_TEST_TAG, modifier = modifier) {
        Text(
            text = taffyString(R.string.taffy_task_view_ask_title),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textPrimary,
        )
        Text(
            text = question,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.testTag(ASK_TEST_TAG),
        )
        OutlinedTextField(
            value = answer,
            onValueChange = { candidate ->
                if (ComposerTextBounds.fits(candidate, MAX_USER_INPUT_ANSWER_BYTES)) {
                    answer = candidate
                }
            },
            label = { Text(text = taffyString(R.string.taffy_task_view_ask_label)) },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
            modifier = Modifier
                .fillMaxWidth()
                .testTag(ASK_FIELD_TEST_TAG),
        )
        if (credentialShaped) {
            Text(
                text = taffyString(R.string.taffy_task_view_ask_credential),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(ASK_CREDENTIAL_TEST_TAG),
            )
        }
        TaffyPrimaryButton(
            label = taffyString(R.string.taffy_task_view_ask_send),
            onClick = { onIntent(AssistantBarIntent.Answer(answer.trim())) },
            enabled = answer.trim().isNotEmpty() && !credentialShaped,
            size = TaffyButtonSize.COMPACT,
            testTag = ASK_SEND_TEST_TAG,
        )
    }
}

/** The frame the takeover band draws in, so the two read as one family. */
@Composable
private fun WaitCard(
    spoken: String,
    testTag: String,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Column(
        modifier = modifier
            .taffyChromeWidth()
            .padding(horizontal = TaffyTheme.spacing.screenMargin)
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.accentWash)
            .border(TaffyBorders.standard, TaffyTheme.colors.accent, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.snug)
            .testTag(testTag)
            .semantics {
                contentDescription = spoken
                liveRegion = LiveRegionMode.Polite
            },
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        content()
    }
}

/**
 * A run of four to eight digits, with spaces or dashes, is the shape of a
 * one-time code; the card will not carry one, because a code goes to the
 * page and never into the AI data plane.
 */
internal fun isCredentialShapedAnswer(answer: String): Boolean {
    var digits = 0
    for (character in answer) {
        when {
            character in '0'..'9' -> digits += 1
            character == ' ' || character == '-' -> Unit
            else -> return false
        }
    }
    return digits in 4..8
}

/** The same frame the Assistant pill reads, so the card and the pill agree. */
private val WaitFrame = TaffyDestination.AssistantBar()

/** The tags the wait card's semantics tests name. */
const val HANDOVER_TEST_TAG: String = "task_wait_handover"
const val HANDOVER_DONE_TEST_TAG: String = "task_wait_hand_back"
const val HANDOVER_STOP_TEST_TAG: String = "task_wait_stop"
const val ASK_CARD_TEST_TAG: String = "task_wait_ask"
const val ASK_TEST_TAG: String = "task_wait_ask_question"
const val ASK_FIELD_TEST_TAG: String = "task_wait_ask_field"
const val ASK_SEND_TEST_TAG: String = "task_wait_ask_send"
const val ASK_CREDENTIAL_TEST_TAG: String = "task_wait_ask_credential"
