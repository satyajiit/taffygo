// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.onRoot
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.controlTestTag
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class TaskResultsSemanticsTest {
    @get:Rule val compose = createComposeRule()

    @Test fun takeOverApprovalAndSaveStayReachableAfterScrollingToTheLastFinding() {
        val intents = mutableListOf<TaskViewIntent>()
        val state = mutableStateOf(AssistantPreviewStates.taskRunning.copy(facts = (0..30).map {
            AssistantPreviewStates.taskRunning.facts.first().copy(id = FactId("fact_$it"), value = "Finding $it")
        }))
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(state.value, onIntent = { intents += it })
            }
        }
        compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasTestTag("${FACT_TEST_TAG_PREFIX}fact_30"))
        compose.onNodeWithTag(controlTestTag(TaskControl.TAKE_OVER)).assertIsDisplayed().performClick()
        captureIfRequested("task-results-scrolled-controls.png")
        compose.runOnIdle { state.value = state.value.copy(approvalActionId = "approval", approvalHost = "shop.example.test", approvalCount = 1) }
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val approvalScope = context.resources.getQuantityString(R.plurals.taffy_task_view_approval_body, 1, 1, "shop.example.test")
        compose.onNodeWithText(approvalScope).assertIsDisplayed()
        compose.onNodeWithTag(APPROVE_TEST_TAG).assertIsDisplayed().performClick()
        compose.onNodeWithTag(DENY_TEST_TAG).assertIsDisplayed()
        compose.runOnIdle { state.value = state.value.copy(state = TaskDisplayState.DONE, controls = emptyList(), approvalActionId = null, canSaveWorkspace = true) }
        compose.onNodeWithTag(SAVE_WORKSPACE_TEST_TAG).assertIsDisplayed().performClick()
        captureIfRequested("task-results-scrolled-save.png")
        assertEquals(listOf(TaskViewIntent.Control(TaskControl.TAKE_OVER), TaskViewIntent.ApproveAction, TaskViewIntent.SaveWorkspace), intents)
    }

    @Test fun recentActivityCanExpandWithoutLosingEarlierSteps() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(TaskViewUiState(taskId = "history", state = TaskDisplayState.DONE,
                    timeline = (6 downTo 1).map { TaskTimelineEntry(it.toLong(), TaskTimelineKind.READ_PAGE, "shop$it.example.test") }), onIntent = {})
            }
        }
        compose.onNodeWithTag("${TIMELINE_ENTRY_TEST_TAG_PREFIX}1").assertDoesNotExist()
        compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasTestTag(TIMELINE_TOGGLE_TEST_TAG))
        compose.onNodeWithTag(TIMELINE_TOGGLE_TEST_TAG).performClick()
        compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasTestTag("${TIMELINE_ENTRY_TEST_TAG_PREFIX}1"))
        compose.onNodeWithTag("${TIMELINE_ENTRY_TEST_TAG_PREFIX}1").assertIsDisplayed()
    }

    /**
     * Nine kinds, nine sentences, and none of them empty (decision 0148).
     *
     * The timeline's whole promise is that a step can only ever render one of a
     * fixed set of sentences, so the thing worth asserting is that every kind
     * has one and that no two kinds say the same thing — a `when` branch
     * pointing at the wrong resource compiles perfectly and reads as the wrong
     * step. The hostless half is asserted beside it because the core declines
     * to name a host it was not consented to, so a step with no host is an
     * ordinary step rather than an edge case.
     */
    @Test fun everyTimelineKindRendersItsOwnSentenceWithAndWithoutAHost() {
        val named = TaskTimelineKind.entries.mapIndexed { index, kind ->
            TaskTimelineEntry((index + 1).toLong(), kind, "shop.example.test", 2, 1_000L)
        }
        val unnamed = TaskTimelineKind.entries.mapIndexed { index, kind ->
            TaskTimelineEntry((index + 1 + named.size).toLong(), kind, null, 0, 1_000L)
        }
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    TaskViewUiState(
                        taskId = "kinds",
                        state = TaskDisplayState.DONE,
                        timeline = named + unnamed,
                    ),
                    onIntent = {},
                )
            }
        }
        compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasTestTag(TIMELINE_TOGGLE_TEST_TAG))
        compose.onNodeWithTag(TIMELINE_TOGGLE_TEST_TAG).performClick()
        val lines = (named + unnamed).map { entry ->
            val tag = "$TIMELINE_ENTRY_TEST_TAG_PREFIX${entry.sequence}"
            compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasTestTag(tag))
            val node = compose.onNodeWithTag(tag, useUnmergedTree = true).fetchSemanticsNode()
            node.config[SemanticsProperties.Text].joinToString(" ") { it.text }.trim()
        }
        assertEquals(emptyList<String>(), lines.filter { it.isEmpty() })
        assertEquals(lines.size, lines.distinct().size)
    }

    @Test fun sourcedFindingsResolveBothOpaqueSourceIdsToTheirActualHosts() {
        val preview = AssistantPreviewStates.taskRunning
        val sources = preview.sources.mapIndexed { index, source -> source.copy(id = SourceId("opaque-$index")) }
        val fact = preview.facts.first().copy(sources = sources.map { it.id })
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(preview.copy(facts = listOf(fact), sources = sources), onIntent = {})
            }
        }
        val tag = "$FACT_TEST_TAG_PREFIX${fact.id.value}"
        compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasTestTag(tag))
        sources.forEach { source ->
            compose.onNode(hasText(source.host) and hasAnyAncestor(hasTestTag(tag)), useUnmergedTree = true).assertIsDisplayed()
            compose.onNodeWithText(source.id.value).assertDoesNotExist()
        }
    }

    @Test fun streamedMarkdownBecomesAccessibleHeadingsAndTableCellsWithoutActiveLinks() {
        val text = mutableStateOf("## Summary\n\n**Harbor")
        val streaming = mutableStateOf(true)
        val dark = mutableStateOf(false)
        compose.setContent {
            TaffyPreview(darkTheme = dark.value, reducedMotion = true) {
                TaskViewContent(TaskViewUiState(taskId = "formatted-comparison", goal = "Compare these phone prices",
                    state = if (streaming.value) TaskDisplayState.RUNNING else TaskDisplayState.DONE,
                    liveAnswer = TaskAnswerProjection(listOf(text.value), streaming.value, false, false),
                    canSaveWorkspace = !streaming.value), onIntent = {})
            }
        }
        compose.waitUntil(5_000) { compose.onAllNodesWithText("Summary").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithText("Summary").assert(SemanticsMatcher.keyIsDefined(SemanticsProperties.Heading))
        compose.onNodeWithText("**Harbor").assertIsDisplayed()
        compose.runOnIdle {
            text.value += "** costs less.\n\n| Store | Price |\n| --- | --- |\n| Orchard | $349 |\n| Harbor | $329 |\n\n[Shop](https://example.test)"
            streaming.value = false
        }
        compose.waitUntil(5_000) { compose.onAllNodesWithText("Harbor costs less.").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasText("$329"))
        compose.onNodeWithText("Store").assertIsDisplayed()
        compose.onNodeWithText("$329").assertIsDisplayed()
        compose.onNodeWithText("Shop (https://example.test)").assertHasNoClickAction()
        compose.onNodeWithTag(SAVE_WORKSPACE_TEST_TAG).assertIsDisplayed()
        captureIfRequested("task-results-formatted-comparison-light.png")
        compose.runOnIdle { dark.value = true }
        captureIfRequested("task-results-formatted-comparison-dark.png")
    }

    private fun captureIfRequested(name: String) {
        if (InstrumentationRegistry.getArguments().getString("captureTaskWorkspace") != "true") return
        compose.waitForIdle()
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        File(context.getExternalFilesDir(null), name).outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
        }
    }
}
