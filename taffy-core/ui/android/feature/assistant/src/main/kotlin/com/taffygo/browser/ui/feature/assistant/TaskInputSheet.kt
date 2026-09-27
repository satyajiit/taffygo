// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyAssistantPill
import com.taffygo.browser.ui.core.ui.TaffyAssistantPillState
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecureWindow
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.rememberScreenViewModelStoreOwner
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The form Taffy is holding open, as a local sheet on whatever screen the
 * person is on. Not a destination.
 *
 * The same shape as the Add pages sheet — a view model scoped to the host
 * destination, opened and closed by its own state — with one thing that sheet
 * does not have: a pill. Putting the sheet away must never answer the request
 * or end the task, so when a request is open and the sheet is not, this draws
 * the waiting pill instead, and its one control brings the sheet back.
 *
 * The pill is drawn here rather than by the Assistant bar on purpose. The bar
 * projects the status plane, and a form is deliberately not on that plane — the
 * bar would have to hold the value seam to know a form was open, and the whole
 * point of the seam is that as few things as possible hold it.
 */
@Composable
fun TaskInputSheet(
    destination: TaffyDestination,
    modifier: Modifier = Modifier,
) {
    val viewModel: TaskInputViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(destination),
        key = "${destination.route}/task-input",
    )
    val state by viewModel.state.collectAsStateWithLifecycle()
    TaskInputSheetContent(
        state = state,
        onIntent = viewModel::onIntent,
        modifier = modifier,
    )
}

/** The stateless half, which is what a preview and a semantics test render. */
@Composable
fun TaskInputSheetContent(
    state: TaskInputUiState,
    onIntent: (TaskInputIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (!state.hasRequest) return
    if (!state.open) {
        TaskInputPill(state = state, onIntent = onIntent, modifier = modifier)
        return
    }
    // Every value on this sheet is one somebody could photograph over a
    // shoulder or a screen recorder could take, so the window is closed for as
    // long as the sheet is composed and no longer.
    TaffySecureWindow()
    TaffyBottomSheet(
        title = taffyString(
            if (state.interactive) {
                R.string.taffy_task_input_interactive_title
            } else if (state.reviewing) {
                R.string.taffy_task_input_review_title
            } else {
                R.string.taffy_task_input_title
            },
        ),
        onDismissRequest = { onIntent(TaskInputIntent.Dismiss) },
        modifier = modifier,
        testTag = TASK_INPUT_SHEET_TEST_TAG,
    ) {
        if (state.interactive) {
            TaskInputInteractiveCard(state = state, onIntent = onIntent)
        } else if (state.reviewing) {
            TaskInputReview(state = state, onIntent = onIntent)
        } else {
            TaskInputForm(state = state, onIntent = onIntent)
        }
        TaskInputDataSentence(host = state.host)
        state.failure?.let { TaskInputFailureLine(reason = it) }
    }
}

/**
 * The waiting pill a dismissed request leaves behind.
 *
 * It carries its own chrome width and margin rather than taking them from the
 * surface it is slotted into, so that a slot with nothing in it composes
 * nothing at all — a wrapper drawn around an empty slot would still be a child
 * of the chrome's column, and a column that spaces its children would leave a
 * gap above the action row on every page in every session.
 */
@Composable
private fun TaskInputPill(
    state: TaskInputUiState,
    onIntent: (TaskInputIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val line = taffyString(R.string.taffy_task_input_pill, state.host)
    TaffyAssistantPill(
        state = TaffyAssistantPillState.WAITING,
        label = line,
        onAction = { onIntent(TaskInputIntent.Reopen) },
        labelModifier = Modifier.semantics { contentDescription = line },
        modifier = modifier
            .taffyChromeWidth()
            .padding(horizontal = TaffyTheme.spacing.screenMargin)
            .fillMaxWidth()
            .testTag(TASK_INPUT_PILL_TEST_TAG),
    )
}

/** Every described row, and the one control that sends them. */
@Composable
private fun TaskInputForm(
    state: TaskInputUiState,
    onIntent: (TaskInputIntent) -> Unit,
) {
    Text(
        text = taffyString(R.string.taffy_task_input_body, state.host),
        style = TaffyTheme.typography.body,
        color = TaffyTheme.colors.textPrimary,
        modifier = Modifier.testTag(TASK_INPUT_BODY_TEST_TAG),
    )
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        state.rows.forEach { row ->
            TaskInputFieldRow(
                row = row,
                host = state.host,
                onValueChange = { onIntent(TaskInputIntent.ValueChanged(row.id, it)) },
            )
        }
    }
    TaffyPrimaryButton(
        label = taffyString(R.string.taffy_task_input_send),
        onClick = { onIntent(TaskInputIntent.Submit) },
        enabled = state.canReview,
        loading = state.submitting,
        testTag = TASK_INPUT_SEND_TEST_TAG,
    )
}

/**
 * The exact-value approval. Values stay in the same transient view-model
 * state used by the editor; this composable only makes every one visible and
 * scrollable before the one call that sends them into the browser vault.
 */
@Composable
private fun TaskInputReview(
    state: TaskInputUiState,
    onIntent: (TaskInputIntent) -> Unit,
) {
    Text(
        text = taffyString(R.string.taffy_task_input_review_action, state.host),
        style = TaffyTheme.typography.body,
        color = TaffyTheme.colors.textPrimary,
        modifier = Modifier.testTag(TASK_INPUT_REVIEW_ACTION_TEST_TAG),
    )
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(max = 280.dp)
            .verticalScroll(rememberScrollState())
            .testTag(TASK_INPUT_REVIEW_VALUES_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        state.rows.forEach { row ->
            Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
                Text(
                    text = row.label,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
                // Deliberately never masked or truncated: this is the exact
                // value the person is approving, inside a secure window.
                Text(
                    text = row.value,
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textPrimary,
                )
            }
        }
    }
    Text(
        text = taffyString(R.string.taffy_task_input_review_reversible),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier.testTag(TASK_INPUT_REVIEW_REVERSIBLE_TEST_TAG),
    )
    Text(
        text = taffyPlural(
            R.plurals.taffy_task_input_review_expiry,
            state.approvalLifetimeSeconds,
            state.approvalLifetimeSeconds,
        ),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier.testTag(TASK_INPUT_REVIEW_EXPIRY_TEST_TAG),
    )
    TaffySecondaryButton(
        label = taffyString(R.string.taffy_task_input_review_edit),
        onClick = { onIntent(TaskInputIntent.Edit) },
        enabled = !state.submitting,
        testTag = TASK_INPUT_REVIEW_EDIT_TEST_TAG,
    )
    TaffyPrimaryButton(
        label = taffyString(R.string.taffy_task_input_review_confirm),
        onClick = { onIntent(TaskInputIntent.Confirm) },
        enabled = state.canConfirm,
        loading = state.submitting,
        testTag = TASK_INPUT_REVIEW_CONFIRM_TEST_TAG,
    )
}

/**
 * What a widget in a cross-origin frame collapses to.
 *
 * There is no way to draw the widget here and there never will be: it belongs
 * to another origin, and nothing in this process may reach into one. So the
 * sheet says what to do, the page area highlights where, and the one control
 * says when it is done.
 */
@Composable
private fun TaskInputInteractiveCard(
    state: TaskInputUiState,
    onIntent: (TaskInputIntent) -> Unit,
) {
    Text(
        text = taffyString(R.string.taffy_task_input_interactive_body, state.host),
        style = TaffyTheme.typography.body,
        color = TaffyTheme.colors.textPrimary,
        modifier = Modifier.testTag(TASK_INPUT_INTERACTIVE_TEST_TAG),
    )
    TaffyPrimaryButton(
        label = taffyString(R.string.taffy_task_input_interactive_done),
        onClick = { onIntent(TaskInputIntent.CompleteInteractive) },
        enabled = state.canCompleteInteractive,
        loading = state.submitting,
        testTag = TASK_INPUT_DONE_TEST_TAG,
    )
}

/**
 * The sentence that is on this sheet whatever else is, and says exactly where
 * what is typed goes.
 *
 * Not a disclosure a person has to open, and not a sentence about mechanisms:
 * it names the site and it names the thing people are actually worried about
 * (voice-and-naming rule 3). It is outside the `interactive` branch above
 * because it is true of both — a widget on the page is worked in on the page,
 * and nothing about it reaches a model either.
 */
@Composable
private fun TaskInputDataSentence(host: String) {
    Text(
        text = taffyString(R.string.taffy_task_input_data, host),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier.testTag(TASK_INPUT_DATA_TEST_TAG),
    )
}

/**
 * One line for a refusal, composed from a closed reason.
 *
 * Two sentences rather than eight: a person can act on "the browser is not
 * answering" and on "that was not accepted", and every other distinction in
 * [FailureReason] is about which part of the browser said no.
 */
@Composable
private fun TaskInputFailureLine(reason: FailureReason) {
    Text(
        text = taffyString(
            if (reason == FailureReason.CORE_UNAVAILABLE) {
                R.string.taffy_task_input_unavailable
            } else {
                R.string.taffy_task_input_refused
            },
        ),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.caution,
        modifier = Modifier.testTag(TASK_INPUT_FAILURE_TEST_TAG),
    )
}

/** The tags the form sheet's tests name. */
const val TASK_INPUT_SHEET_TEST_TAG: String = "task_input_sheet"
const val TASK_INPUT_PILL_TEST_TAG: String = "task_input_pill"
const val TASK_INPUT_BODY_TEST_TAG: String = "task_input_body"
const val TASK_INPUT_DATA_TEST_TAG: String = "task_input_data"
const val TASK_INPUT_SEND_TEST_TAG: String = "task_input_send"
const val TASK_INPUT_INTERACTIVE_TEST_TAG: String = "task_input_interactive"
const val TASK_INPUT_DONE_TEST_TAG: String = "task_input_done"
const val TASK_INPUT_FAILURE_TEST_TAG: String = "task_input_failure"
const val TASK_INPUT_REVIEW_ACTION_TEST_TAG: String = "task_input_review_action"
const val TASK_INPUT_REVIEW_VALUES_TEST_TAG: String = "task_input_review_values"
const val TASK_INPUT_REVIEW_REVERSIBLE_TEST_TAG: String = "task_input_review_reversible"
const val TASK_INPUT_REVIEW_EXPIRY_TEST_TAG: String = "task_input_review_expiry"
const val TASK_INPUT_REVIEW_EDIT_TEST_TAG: String = "task_input_review_edit"
const val TASK_INPUT_REVIEW_CONFIRM_TEST_TAG: String = "task_input_review_confirm"
