// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.TaskConversationProjection
import com.taffygo.browser.ui.core.task.TaskExchange
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.rememberScreenViewModelStoreOwner
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The conversation in the Ask overlay: what the person asked, what Taffy
 * answered under it, and what Taffy is doing now (decisions 0135, 0137).
 *
 * Each exchange is the person's words and the answer beneath them, oldest
 * first; the newest may still be arriving. Under the exchanges stands the
 * start page's task panel without its title — the goal is already the first
 * question — carrying the moving line, the rail, the controls the core
 * admits, and the two doors once the task has ended. It is the assistant
 * feature's and reaches the overlay as a slot filled by the shell, because
 * browsing does not depend on the assistant.
 */
@Composable
fun AskConversationPanel(
    navigator: TaffyNavigator,
    taskId: String,
    goal: String,
    onTryAgain: () -> Unit,
    onLeave: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val barModel: AssistantBarViewModel = screenViewModel(ConversationFrame)
    val bar by barModel.state.collectAsStateWithLifecycle()
    // Under a key of its own, as the report model below is. A view-model store
    // holds one model per key and clears the one it replaces, so asking for
    // this under the frame's route evicted the bar's model — the one the pill
    // in the browser's chrome shares — and the pill stopped on the phase it
    // showed when the conversation first drew, "Taffy is planning…" with Take
    // over, until something else recomposed it.
    val conversationModel: AskConversationViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(ConversationFrame, CONVERSATION_KEY),
        key = CONVERSATION_KEY,
    )
    val repository by conversationModel.state.collectAsStateWithLifecycle()
    val reportModel: ReportAnswerViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(ConversationFrame),
        key = "${ConversationFrame.route}/report-answer",
    )
    val report by reportModel.state.collectAsStateWithLifecycle()
    val conversation = askConversation(repository, taskId, goal)
    Column(
        modifier = modifier
            .fillMaxWidth()
            .testTag(ASK_CONVERSATION_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        conversation.exchanges.forEachIndexed { index, exchange ->
            key(index) {
                Exchange(exchange, index) { answer ->
                    reportModel.onIntent(ReportAnswerIntent.Open(answer), navigator)
                }
            }
        }
        StartPageTaskPanelContent(
            state = bar,
            taskId = taskId,
            goal = goal,
            onIntent = { barModel.onIntent(it, navigator) },
            onTryAgain = onTryAgain,
            onLeave = onLeave,
            onOpenSetup = { navigator.goTo(TaffyDestination.AiAndProviders) },
            showsGoal = false,
            leaveLabel = taffyString(R.string.taffy_ask_conversation_close),
        )
    }
    ReportAnswerSheet(state = report, onIntent = { reportModel.onIntent(it, navigator) })
}

@Composable
private fun Exchange(
    exchange: TaskExchange,
    index: Int,
    onReport: (TaskAnswerProjection) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        Text(
            text = exchange.question,
            style = TaffyTheme.typography.body.copy(fontWeight = FontWeight.Medium),
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.testTag("$ASK_QUESTION_TEST_TAG_PREFIX$index"),
        )
        exchange.answer?.takeIf { it.hasSomethingToShow() }?.let { answer ->
            TaskAnswerBlock(
                answer = answer,
                modifier = Modifier.testTag("$ASK_ANSWER_TEST_TAG_PREFIX$index"),
                onReport = { onReport(answer) },
            )
        }
    }
}

/**
 * The conversation to draw for [taskId]: the repository's, when the
 * followed task is that one and has one; otherwise the goal alone, with no
 * answer yet — which is what the person sees between the start being
 * admitted and the first delta arriving, and what they see when the core's
 * list no longer holds the task at all.
 */
internal fun askConversation(
    repository: TaskRepositoryState,
    taskId: String,
    goal: String,
): TaskConversationProjection =
    repository.task?.takeIf { it.id == taskId }?.conversation
        ?: TaskConversationProjection(listOf(TaskExchange(question = goal, answer = null)))

/**
 * The bar's own fixed identity, so the panel and the pill share one view
 * model over one followed task rather than projecting it twice.
 */
private val ConversationFrame = TaffyDestination.AssistantBar()

/** The conversation model's key on the frame's entry, apart from the bar's. */
private val CONVERSATION_KEY = "${ConversationFrame.route}/conversation"

/** The tags the semantics tests name. */
const val ASK_CONVERSATION_TEST_TAG: String = "ask_conversation"
const val ASK_QUESTION_TEST_TAG_PREFIX: String = "ask_question_"
const val ASK_ANSWER_TEST_TAG_PREFIX: String = "ask_answer_"
