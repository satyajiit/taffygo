// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffyStatusChip
import com.taffygo.browser.ui.core.ui.rememberScreenViewModelStoreOwner
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString
import kotlinx.coroutines.launch

/**
 * Screen SCR-303 — the task view.
 *
 * Real pages and activity lead into the answer and sources. Current task
 * controls stay in a fixed footer while the result scrolls.
 */
@Composable
fun TaskViewScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: TaskViewViewModel = screenViewModel(TaffyDestination.TaskView)
    val state by viewModel.state.collectAsStateWithLifecycle()
    val browserViewModel: TaskBrowserViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(TaffyDestination.TaskView),
        key = "${TaffyDestination.TaskView.route}/browser",
    )
    val browserState by browserViewModel.state.collectAsStateWithLifecycle()
    val skillReviewViewModel: TaskSkillReviewViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(TaffyDestination.TaskView),
        key = "${TaffyDestination.TaskView.route}/skill-review",
    )
    val skillReviewState by skillReviewViewModel.state.collectAsStateWithLifecycle()
    val downloadsViewModel: TaskDownloadsViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(TaffyDestination.TaskView),
        key = "${TaffyDestination.TaskView.route}/downloads",
    )
    val downloadsState by downloadsViewModel.state.collectAsStateWithLifecycle()
    val reportViewModel: ReportAnswerViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(TaffyDestination.TaskView),
        key = "${TaffyDestination.TaskView.route}/report-answer",
    )
    val reportState by reportViewModel.state.collectAsStateWithLifecycle()
    val context = LocalContext.current.applicationContext
    val ioDispatcher = LocalAppDispatchers.current.io
    val coroutineScope = rememberCoroutineScope()
    val createDocument = rememberLauncherForActivityResult(CreateTaskArtifactDocument()) { uri ->
        val pending = viewModel.state.value.pendingArtifactExport
            ?: return@rememberLauncherForActivityResult
        if (pending.destination != TaskViewIntent.ArtifactDestination.CREATE_DOCUMENT) {
            return@rememberLauncherForActivityResult
        }
        val requestId = pending.payload.requestId
        if (uri == null) {
            viewModel.onIntent(TaskViewIntent.ArtifactExportHandled(requestId, true))
            return@rememberLauncherForActivityResult
        }
        coroutineScope.launch {
            val written = writeTaskArtifact(
                resolver = context.contentResolver,
                dispatcher = ioDispatcher,
                uri = uri,
                payload = pending.payload,
            )
            viewModel.onIntent(TaskViewIntent.ArtifactExportHandled(requestId, written))
        }
    }

    LaunchedEffect(Unit) { viewModel.onShown() }
    LaunchedEffect(state.pendingArtifactExport?.payload?.requestId) {
        val pending = state.pendingArtifactExport ?: return@LaunchedEffect
        when (pending.destination) {
            TaskViewIntent.ArtifactDestination.CREATE_DOCUMENT -> try {
                createDocument.launch(
                    TaskArtifactDocumentSpec(
                        mimeType = pending.artifact.mimeType,
                        suggestedName = pending.artifact.suggestedFileName,
                    ),
                )
            } catch (_: RuntimeException) {
                viewModel.onIntent(
                    TaskViewIntent.ArtifactExportHandled(pending.payload.requestId, false),
                )
            }
            TaskViewIntent.ArtifactDestination.SHARE -> {
                val shared = shareTaskArtifact(
                    context = context,
                    dispatcher = ioDispatcher,
                    artifact = pending.artifact,
                    payload = pending.payload,
                    chooserTitle = context.getString(
                        R.string.taffy_task_view_file_share_chooser,
                    ),
                )
                viewModel.onIntent(
                    TaskViewIntent.ArtifactExportHandled(pending.payload.requestId, shared),
                )
            }
        }
    }

    TaskViewContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = { navigator.goBack() },
        onOpenSetup = { navigator.goTo(TaffyDestination.AiAndProviders) },
        browserState = browserState,
        onOpenTab = { browserViewModel.openTab(state.taskId, it, navigator) },
        skillReviewState = skillReviewState,
        downloadsState = downloadsState,
        onOpenDownload = { downloadsViewModel.open(state.taskId, it) },
        onShowDownloads = { navigator.goTo(TaffyDestination.Downloads) },
        onReviewFlow = skillReviewViewModel::reviewCurrent,
        onDismissFlow = skillReviewViewModel::dismissCurrent,
        onAcceptFlow = { skillReviewState.review?.let(skillReviewViewModel::accept) },
        onCloseFlowReview = skillReviewViewModel::closeReview,
        onReportAnswer = { reportViewModel.onIntent(ReportAnswerIntent.Open(it), navigator) },
        modifier = modifier,
    )
    ReportAnswerSheet(state = reportState, onIntent = { reportViewModel.onIntent(it, navigator) })
}

/** The stateless half. */
@Composable
fun TaskViewContent(
    state: TaskViewUiState,
    onIntent: (TaskViewIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    onOpenSetup: (() -> Unit)? = null,
    browserState: TaskBrowserUiState = TaskBrowserUiState(),
    onOpenTab: (TabId) -> Unit = {},
    skillReviewState: TaskSkillReviewUiState = TaskSkillReviewUiState(),
    onReviewFlow: () -> Unit = {},
    onDismissFlow: () -> Unit = {},
    onAcceptFlow: () -> Unit = {},
    onCloseFlowReview: () -> Unit = {},
    downloadsState: TaskDownloadsUiState = TaskDownloadsUiState(),
    onOpenDownload: (DownloadId) -> Unit = {},
    onShowDownloads: () -> Unit = {},
    onReportAnswer: ((TaskAnswerProjection) -> Unit)? = null,
) {
    val hasTaskDownloads = downloadsState.taskId == state.taskId && downloadsState.files.isNotEmpty() &&
        state.state in listOf(TaskDisplayState.DONE, TaskDisplayState.PARTLY_DONE, TaskDisplayState.FAILED)
    val footerDownload = if (hasTaskDownloads) downloadsState.files.firstOrNull {
        it.mimeType.equals("application/pdf", true)
    } ?: downloadsState.files.firstOrNull() else null
    TaffyScreen(
        destination = TaffyDestination.TaskView,
        // The request is what this screen is about, so it is the title, and the
        // eyebrow that used to sit over it in the body is the subtitle — the
        // shape SCR-305 and the skill detail screen already title themselves
        // with. The bar itself is the shared one: Back with the word beside its
        // chevron on its own row, the title beneath it, the state on the
        // trailing edge. It used to be a hand-rolled row with an icon-only
        // chevron and the word "Taffy" in label type, and it was the only
        // pushed, back-capable screen in the product not in this frame.
        title = state.goal.ifEmpty { taffyString(R.string.taffy_task_view_no_task_title) },
        subtitle = taffyString(R.string.taffy_task_results_request),
        onBack = onBack,
        toolbarRight = {
            state.state?.let {
                TaffyStatusChip(
                    StatusPresentation.of(it),
                    Modifier.testTag(TASK_HEADER_TEST_TAG),
                )
            }
        },
        footer = {
            TaskResultsFooter(state, downloadsState, footerDownload, onIntent, onOpenDownload, onShowDownloads, onOpenSetup,
                skillReviewState.takeIf { it.taskId == state.taskId && state.state == TaskDisplayState.DONE },
                onReviewFlow, onDismissFlow)
        },
        scrollable = false,
        modifier = modifier,
    ) {
        TaskViewLayout(
            state = state,
            header = {
                TaskHeader(state = state, onIntent = onIntent, onOpenSetup = onOpenSetup)
            },
            onIntent = onIntent,
            browserState = browserState,
            onOpenTab = onOpenTab,
            hasTaskDownloads = hasTaskDownloads,
            downloads = {
                if (hasTaskDownloads) TaskDownloadsPanel(downloadsState, onOpenDownload, onShowDownloads, footerDownload?.id)
            },
            flowReview = {
                if (skillReviewState.taskId == state.taskId && state.state == TaskDisplayState.DONE) {
                    TaskSkillReviewPanel(skillReviewState, onReviewFlow, onDismissFlow, footerActions = true)
                }
            },
            onReportAnswer = onReportAnswer,
        )
    }
    if (skillReviewState.taskId == state.taskId && state.state == TaskDisplayState.DONE) {
        TaskSkillReviewDialog(skillReviewState, onAcceptFlow, onCloseFlowReview)
    }
    if (state.showDiscardConfirmation) {
        AlertDialog(
            onDismissRequest = { onIntent(TaskViewIntent.CancelDiscardWorkspace) },
            title = { Text(taffyString(R.string.taffy_task_view_discard_title)) },
            text = { Text(taffyString(R.string.taffy_task_view_discard_body)) },
            confirmButton = {
                TaffyDangerButton(
                    label = taffyString(R.string.taffy_task_view_discard_confirm),
                    onClick = { onIntent(TaskViewIntent.ConfirmDiscardWorkspace) },
                    testTag = DISCARD_WORKSPACE_CONFIRM_TEST_TAG,
                )
            },
            dismissButton = {
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_task_view_discard_cancel),
                    onClick = { onIntent(TaskViewIntent.CancelDiscardWorkspace) },
                )
            },
        )
    }
}

@Composable
private fun TaskHeader(
    state: TaskViewUiState,
    onIntent: (TaskViewIntent) -> Unit,
    onOpenSetup: (() -> Unit)?,
) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        // No hero card. It said "your request" over the goal, and both are the
        // bar's now — its title and its subtitle — so what stood here was a
        // decorative plate above the timeline saying nothing.
        //
        // The task keeps its state word; this notice explains when the current process cannot drive it.
        state.notice?.let {
            androidx.compose.material3.Text(
                text = taffyString(noticeChipLine(it)),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(NOT_DRIVEN_TEST_TAG),
            )
        }
        TaskFailureSection(state = state, onOpenSetup = onOpenSetup)
        if (state.workspaceSaved) {
            Text(taffyString(R.string.taffy_task_view_workspace_saved),
                style = TaffyTheme.typography.detail, color = TaffyTheme.colors.textSecondary)
        }
        val suggestion = state.rememberThis
        if (suggestion != null && state.state?.isFinal == true) {
            RememberThisCard(
                suggestion = suggestion,
                onRemember = { onIntent(TaskViewIntent.RememberThis) },
                onNotNow = { onIntent(TaskViewIntent.DismissRememberThis) },
            )
        }
    }
}

const val DISCARD_WORKSPACE_TEST_TAG: String = "task_view_discard_workspace"
const val DISCARD_WORKSPACE_CONFIRM_TEST_TAG: String = "task_view_discard_workspace_confirm"

/** The tags screen SCR-303's semantics tests name. */
const val TASK_HEADER_TEST_TAG: String = "task_view_header"
const val RETRY_TEST_TAG: String = "task_view_retry"
const val SAVE_WORKSPACE_TEST_TAG: String = "task_view_save_workspace"
const val NOT_DRIVEN_TEST_TAG: String = "task_view_not_driven"
const val APPROVAL_TEST_TAG: String = "task_view_approval"
const val APPROVE_TEST_TAG: String = "task_view_approve"
const val DENY_TEST_TAG: String = "task_view_deny"
