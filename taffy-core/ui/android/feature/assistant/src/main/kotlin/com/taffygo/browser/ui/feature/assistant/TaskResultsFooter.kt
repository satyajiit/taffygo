// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.ui.TAFFY_SETUP_NEEDED_PRIMARY_TEST_TAG
import com.taffygo.browser.ui.core.ui.TAFFY_SETUP_NEEDED_SECONDARY_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.controlTestTag
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString
import com.taffygo.browser.ui.core.ui.taskControlLabel

/** One fixed primary action; all controls still come from the current task revision. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TaskResultsFooter(
    state: TaskViewUiState,
    downloads: TaskDownloadsUiState,
    file: DownloadRecord?,
    onIntent: (TaskViewIntent) -> Unit,
    onOpenDownload: (DownloadId) -> Unit,
    onShowDownloads: () -> Unit,
    onOpenSetup: (() -> Unit)?,
    flow: TaskSkillReviewUiState?,
    onReviewFlow: () -> Unit,
    onDismissFlow: () -> Unit,
) {
    val actions = buildList {
        if (state.hasApproval) add(ResultAction(R.string.taffy_task_view_approve, APPROVE_TEST_TAG) {
            onIntent(TaskViewIntent.ApproveAction)
        })
        if (file != null) {
            if (downloads.failed == file.id) add(ResultAction(R.string.taffy_task_file_show_downloads, TASK_DOWNLOADS_VIEW_TEST_TAG, action = onShowDownloads))
            else add(ResultAction(
                label = when {
                    downloads.opening == file.id -> R.string.taffy_task_file_opening
                    file.mimeType.equals("application/pdf", true) -> R.string.taffy_task_pdf_open
                    else -> R.string.taffy_task_file_open
                },
                tag = "$TASK_DOWNLOAD_OPEN_TEST_TAG_PREFIX${file.id.value}",
                enabled = downloads.opening == null,
            ) { onOpenDownload(file.id) })
        }
        if (state.retryAvailable) add(ResultAction(R.string.taffy_task_view_retry, RETRY_TEST_TAG) { onIntent(TaskViewIntent.RetryCore) })
        if (state.offersSetup && onOpenSetup != null) add(ResultAction(
            R.string.taffy_setup_needed_primary, TAFFY_SETUP_NEEDED_PRIMARY_TEST_TAG, action = onOpenSetup,
        ))
        listOf(TaskControl.TAKE_OVER, TaskControl.RESUME, TaskControl.PAUSE, TaskControl.STOP)
            .filter { it in state.controls }.forEach { control ->
                add(ResultAction(taskControlLabel(control), controlTestTag(control)) { onIntent(TaskViewIntent.Control(control)) })
            }
        if (flow != null && !flow.saved && (flow.review != null || flow.skillId != null)) {
            add(ResultAction(if (flow.loading) R.string.taffy_task_flow_loading else R.string.taffy_task_flow_review,
                TASK_FLOW_REVIEW_TEST_TAG, enabled = !flow.loading, action = onReviewFlow))
        }
        if (state.canSaveWorkspace) add(ResultAction(R.string.taffy_task_view_save_workspace, SAVE_WORKSPACE_TEST_TAG) {
            onIntent(TaskViewIntent.SaveWorkspace)
        })
        if (state.hasApproval) add(ResultAction(R.string.taffy_task_view_deny, DENY_TEST_TAG) { onIntent(TaskViewIntent.DenyAction) })
        if (state.offersSetup && onOpenSetup != null) add(ResultAction(
            R.string.taffy_setup_needed_secondary, TAFFY_SETUP_NEEDED_SECONDARY_TEST_TAG,
        ) { onIntent(TaskViewIntent.DismissSetup) })
        if (flow != null && !flow.saved && (flow.review != null || flow.skillId != null)) {
            add(ResultAction(R.string.taffy_task_flow_not_now, "task_flow_dismiss", action = onDismissFlow))
        }
        if (state.canDiscardWorkspace) add(ResultAction(R.string.taffy_task_view_discard_workspace, DISCARD_WORKSPACE_TEST_TAG) {
            onIntent(TaskViewIntent.RequestDiscardWorkspace)
        })
        // Last, so it never outranks Retry, Approve or a control: this is the
        // door out of a task that has ended, not an answer to it. Decision
        // 0141 keeps the ended pill to one line and no chip, so the affordance
        // lives on the task view the pill opens — the same place Stop lives.
        if (state.state?.isFinal == true) add(ResultAction(R.string.taffy_task_view_put_away, PUT_AWAY_TEST_TAG) {
            onIntent(TaskViewIntent.PutTaskAway)
        })
    }
    if (actions.isEmpty() && !state.workspaceChangeInFlight &&
        state.workspaceChangeRefusal == null && state.controlRefusal == null
    ) {
        return
    }
    Column(Modifier.fillMaxWidth().testTag(TASK_RESULTS_FOOTER_TEST_TAG)) {
        ControlRefusalNotice(state, onIntent)
        WorkspaceChangeNotice(state, onIntent)
        if (state.hasApproval) {
            Text(taffyString(R.string.taffy_task_view_approval_title), style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary)
            Text(taffyPlural(R.plurals.taffy_task_view_approval_body, state.approvalCount,
                state.approvalCount, state.approvalHost.orEmpty()), style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.padding(bottom = TaffyTheme.spacing.tight).testTag(APPROVAL_TEST_TAG))
        }
        val primary = actions.firstOrNull()
        if (primary != null) TaffyPrimaryButton(taffyString(primary.label), primary.action,
            Modifier.fillMaxWidth(), enabled = primary.enabled, testTag = primary.tag)
        if (actions.size > 1) FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceEvenly,
        ) {
            actions.drop(1).forEach { action ->
                TextButton(action.action, enabled = action.enabled, modifier = Modifier.testTag(action.tag)) {
                    Text(taffyString(action.label), style = TaffyTheme.typography.label, color = TaffyTheme.colors.textPrimary)
                }
            }
        }
    }
}

/**
 * What the browser answered about the last control the person pressed.
 *
 * Above the buttons rather than below them, because it is about one of them.
 * The control list is the core's and the authority to honour it is the
 * browser's, and only the browser can say no: a task paused before a browser
 * restart is restored with Resume still on offer and no consent behind it, so
 * the submission is refused on admission and the screen showed nothing at all
 * (decision 0221).
 *
 * Resume gets its own sentence because it is the one with a move behind it —
 * the task cannot be continued, so stopping it and asking again is what works.
 * Every other control shares one, on the same argument the workspace notice
 * below already makes: a person cannot act on the difference between the
 * reasons.
 */
@Composable
private fun ControlRefusalNotice(state: TaskViewUiState, onIntent: (TaskViewIntent) -> Unit) {
    val refusal = state.controlRefusal ?: return
    Row(
        modifier = Modifier.fillMaxWidth().padding(bottom = TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = taffyString(
                if (refusal.control == TaskControl.RESUME) {
                    R.string.taffy_task_view_resume_refused
                } else {
                    R.string.taffy_task_view_control_refused
                },
            ),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.danger,
            modifier = Modifier.weight(1f).testTag(CONTROL_REFUSAL_TEST_TAG),
        )
        TextButton(
            onClick = { onIntent(TaskViewIntent.DismissControlRefusal) },
            modifier = Modifier.testTag(CONTROL_REFUSAL_DISMISS_TEST_TAG),
        ) {
            Text(
                taffyString(R.string.taffy_task_view_workspace_refusal_dismiss),
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary,
            )
        }
    }
}

/**
 * What the core answered about the last save or discard.
 *
 * These commands are answered on admission, not on completion, so a confirmed
 * discard used to close its dialog and change nothing a person could see. That
 * is indistinguishable from a refusal, and a refusal was thrown away unread.
 * Three states are worth three different sentences: the change is running, the
 * same change is already running and this one was a repeat, or it was refused.
 * The refusal carries its own dismissal, because an explanation that cannot be
 * put away is the defect this screen already has one of.
 */
@Composable
private fun WorkspaceChangeNotice(state: TaskViewUiState, onIntent: (TaskViewIntent) -> Unit) {
    val refusal = state.workspaceChangeRefusal
    if (refusal == null) {
        if (!state.workspaceChangeInFlight) return
        Text(
            text = taffyString(R.string.taffy_task_view_workspace_working),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier
                .padding(bottom = TaffyTheme.spacing.tight)
                .testTag(WORKSPACE_CHANGE_WORKING_TEST_TAG),
        )
        return
    }
    Row(
        modifier = Modifier.fillMaxWidth().padding(bottom = TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            // Duplicate means wait, not it failed: the workspace is on its way
            // out and saying "refused" here would be the wrong sentence.
            text = taffyString(
                if (refusal == FailureReason.DUPLICATE) {
                    R.string.taffy_task_view_workspace_busy
                } else {
                    R.string.taffy_task_view_workspace_refused
                },
            ),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.danger,
            modifier = Modifier.weight(1f).testTag(WORKSPACE_CHANGE_REFUSAL_TEST_TAG),
        )
        TextButton(
            onClick = { onIntent(TaskViewIntent.DismissWorkspaceChangeRefusal) },
            modifier = Modifier.testTag(WORKSPACE_CHANGE_DISMISS_TEST_TAG),
        ) {
            Text(
                taffyString(R.string.taffy_task_view_workspace_refusal_dismiss),
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary,
            )
        }
    }
}

private data class ResultAction(val label: Int, val tag: String, val enabled: Boolean = true, val action: () -> Unit)


const val TASK_RESULTS_FOOTER_TEST_TAG: String = "task_results_footer"

const val CONTROL_REFUSAL_TEST_TAG: String = "task_view_control_refusal"

const val CONTROL_REFUSAL_DISMISS_TEST_TAG: String = "task_view_control_refusal_dismiss"

const val WORKSPACE_CHANGE_WORKING_TEST_TAG: String = "task_view_workspace_working"

const val WORKSPACE_CHANGE_REFUSAL_TEST_TAG: String = "task_view_workspace_refusal"

const val WORKSPACE_CHANGE_DISMISS_TEST_TAG: String = "task_view_workspace_dismiss"

const val PUT_AWAY_TEST_TAG: String = "task_view_put_away"
