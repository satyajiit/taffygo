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
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyControlBar
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffyStatusChip
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * What Taffy is doing, drawn where the request was typed.
 *
 * The start page's box starts an errand in place, and until Taffy has a page
 * to be on this panel is the whole of what the person sees of the task: their
 * own words, the moving line the pill would carry, a rail, and the controls
 * the core admits for this exact revision. It reads the same projection as
 * screen SCR-301 — [AssistantBarViewModel] over the followed task — so the
 * line here and the line on the pill can never disagree about the same
 * moment. It is the assistant feature's, and reaches the start page as a slot
 * filled by the shell, because browsing does not depend on the assistant.
 *
 * The panel is about one task, [taskId], and says so: while the followed task
 * is not yet that one — the start was admitted and the first publication has
 * not arrived — it says Taffy is starting rather than showing whatever the
 * core is holding.
 *
 * Once the task has ended the panel is not the end of the road. It offers
 * two doors, and both belong to the box it stands in for, so they arrive as
 * [onTryAgain] and [onLeave] rather than as intents of this feature: the same
 * request again, and home. A task that finished offers its results in the
 * first door's place, and one that could not reach a provider because none
 * is set up offers the set-up screen there, since the same request into the
 * same absence would end the same way.
 */
@Composable
fun StartPageTaskPanel(
    navigator: TaffyNavigator,
    taskId: String,
    goal: String,
    onTryAgain: () -> Unit,
    onLeave: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val viewModel: AssistantBarViewModel = screenViewModel(PanelFrame)
    val state by viewModel.state.collectAsStateWithLifecycle()
    StartPageTaskPanelContent(
        state = state,
        taskId = taskId,
        goal = goal,
        onIntent = { viewModel.onIntent(it, navigator) },
        onTryAgain = onTryAgain,
        onLeave = onLeave,
        modifier = modifier,
        onOpenSetup = { navigator.goTo(TaffyDestination.AiAndProviders) },
    )
}

/**
 * The stateless half, which a preview and a semantics test render.
 *
 * [onOpenSetup] is the screen's navigation, handed in the way the task view
 * hands it to its failure section; a caller with no way to open AI &
 * providers draws the same request again in its place. [showsGoal] is off
 * for a caller that has already drawn the goal above this panel — the Ask
 * overlay's conversation, where it is the first question — and [leaveLabel]
 * names the second door for a caller whose leaving is not going home.
 */
@Composable
fun StartPageTaskPanelContent(
    state: AssistantBarUiState,
    taskId: String,
    goal: String,
    onIntent: (AssistantBarIntent) -> Unit,
    onTryAgain: () -> Unit,
    onLeave: () -> Unit,
    modifier: Modifier = Modifier,
    onOpenSetup: (() -> Unit)? = null,
    showsGoal: Boolean = true,
    leaveLabel: String? = null,
) {
    val isThisTask = state.taskId == taskId
    val line = if (isThisTask) {
        assistantLine(state)
    } else {
        taffyString(R.string.taffy_assistant_running_start)
    }
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card,)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card,)
            .padding(TaffyTheme.spacing.snug)
            .testTag(START_PAGE_TASK_PANEL_TEST_TAG)
            .semantics { liveRegion = LiveRegionMode.Polite },
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        if (showsGoal) {
            Text(
                text = goal,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.testTag(START_PAGE_TASK_GOAL_TEST_TAG),
            )
        }
        if (isThisTask) {
            state.state?.let { TaffyStatusChip(presentation = StatusPresentation.of(it)) }
        }
        Text(
            text = line,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier
                .testTag(START_PAGE_TASK_LINE_TEST_TAG)
                .semantics { contentDescription = line },
        )
        // The rail moves for as long as the task does. It is determinate only
        // once the core has said how far along it is; before that a rail
        // standing at nought would be a claim of no progress, which is not
        // what is known.
        val underWay = isThisTask && state.state?.isFinal == false
        if (!isThisTask || underWay) {
            if (isThisTask && state.progressBasisPoints > 0) {
                LinearProgressIndicator(
                    progress = { state.progressBasisPoints / BASIS_POINTS },
                    modifier = Modifier.fillMaxWidth(),
                    color = TaffyTheme.colors.accent,
                )
            } else {
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth(),
                    color = TaffyTheme.colors.accent,
                )
            }
        }
        if (isThisTask && state.controls.isNotEmpty()) {
            TaffyControlBar(
                controls = state.controls,
                onControl = { onIntent(AssistantBarIntent.Control(it)) },
            )
        }
        if (isThisTask && state.state?.isFinal == true) {
            EndedTaskDoors(
                state = state,
                onIntent = onIntent,
                onTryAgain = onTryAgain,
                onLeave = onLeave,
                onOpenSetup = onOpenSetup,
                leaveLabel = leaveLabel ?: taffyString(R.string.taffy_start_page_task_home),
            )
        }
    }
}

/**
 * The two doors out of an ended task, drawn where the controls stood.
 *
 * Without them the panel was where the road ended: the goal, the word
 * Failed, and nothing to press. Home is always the second door. The first is
 * the results when there are some, set-up when nothing is set up to answer,
 * and the same request again otherwise — a task that was stopped, or that
 * failed under a provider the person can see is connected, is one a person
 * reasonably sends again unchanged.
 */
@Composable
private fun EndedTaskDoors(
    state: AssistantBarUiState,
    onIntent: (AssistantBarIntent) -> Unit,
    onTryAgain: () -> Unit,
    onLeave: () -> Unit,
    onOpenSetup: (() -> Unit)?,
    leaveLabel: String,
) {
    Row(horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        when {
            state.state == TaskDisplayState.DONE || state.state == TaskDisplayState.PARTLY_DONE ->
                TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_start_page_task_results),
                    onClick = { onIntent(AssistantBarIntent.OpenResults) },
                    size = TaffyButtonSize.COMPACT,
                    testTag = START_PAGE_TASK_RESULTS_TEST_TAG,
                )
            state.readiness.needsSetup && onOpenSetup != null ->
                TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_setup_needed_primary),
                    onClick = onOpenSetup,
                    size = TaffyButtonSize.COMPACT,
                    testTag = START_PAGE_TASK_SETUP_TEST_TAG,
                )
            else ->
                TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_start_page_task_try_again),
                    onClick = onTryAgain,
                    size = TaffyButtonSize.COMPACT,
                    testTag = START_PAGE_TASK_TRY_AGAIN_TEST_TAG,
                )
        }
        TaffySecondaryButton(
            label = leaveLabel,
            onClick = onLeave,
            size = TaffyButtonSize.COMPACT,
            testTag = START_PAGE_TASK_HOME_TEST_TAG,
        )
    }
}

/**
 * The bar's own fixed identity, so the panel and the pill share one view
 * model over one followed task rather than projecting it twice.
 */
private val PanelFrame = TaffyDestination.AssistantBar()

private const val BASIS_POINTS = 10_000f

/** The tags the semantics tests name. */
const val START_PAGE_TASK_PANEL_TEST_TAG: String = "start_page_task_panel"
const val START_PAGE_TASK_GOAL_TEST_TAG: String = "start_page_task_goal"
const val START_PAGE_TASK_LINE_TEST_TAG: String = "start_page_task_line"
const val START_PAGE_TASK_TRY_AGAIN_TEST_TAG: String = "start_page_task_try_again"
const val START_PAGE_TASK_RESULTS_TEST_TAG: String = "start_page_task_results"
const val START_PAGE_TASK_SETUP_TEST_TAG: String = "start_page_task_setup"
const val START_PAGE_TASK_HOME_TEST_TAG: String = "start_page_task_home"
