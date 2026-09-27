// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.text.input.ImeAction
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-304 — every workspace, in the state it really reached.
 *
 * A saved partly-done workspace stays partly done in the list, and a stopped
 * one is never dressed up as finished. Both are asserted as words a screen
 * reader hears, not as colours.
 */
class WorkspaceListSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<WorkspaceListIntent>()

    @Test
    fun eachWorkspaceKeepsItsOwnStateWord() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceListContent(
                    state = WorkspacePreviewStates.list,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.WorkspaceList.screenId).assertExists()
        listOf(
            TaskDisplayState.PARTLY_DONE,
            TaskDisplayState.DONE,
            TaskDisplayState.STOPPED,
        ).forEach { state ->
            val word = context.getString(StatusPresentation.of(state).labelRes)
            compose
                .onNode(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, word))
                .assertExists()
        }
    }

    @Test
    fun aRowIsOneAnnouncementNamingTheGoalTheStateAndTheSources() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceListContent(
                    state = WorkspacePreviewStates.list,
                    onIntent = { intents += it },
                )
            }
        }

        val expected = context.getString(
            R.string.taffy_workspace_list_description,
            "compare prices across two listings",
            context.getString(StatusPresentation.of(TaskDisplayState.PARTLY_DONE).labelRes),
            context.resources.getQuantityString(R.plurals.taffy_workspace_list_sources, 1, 1),
        )
        compose.onNodeWithTag("${WORKSPACE_TEST_TAG_PREFIX}ws_partly")
            .assertContentDescriptionEquals(expected)
    }

    @Test
    fun openingAWorkspaceSendsItsIdentifier() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                WorkspaceListContent(
                    state = WorkspacePreviewStates.list,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${WORKSPACE_TEST_TAG_PREFIX}ws_done").performClick()

        assertEquals(listOf(WorkspaceListIntent.Open(WorkspaceId("ws_done"))), intents)
    }

    @Test
    fun typingInTheSearchSendsTheQuery() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceListContent(
                    state = WorkspacePreviewStates.list,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SEARCH_TEST_TAG).performTextReplacement("policies")

        assertEquals(listOf(WorkspaceListIntent.QueryChanged("policies")), intents)
    }

    /**
     * The goals-and-sources field asks the keyboard for Search, rather than
     * taking the single-line default of Done that says nothing about what the
     * field is for.
     */
    @Test
    fun theSearchFieldOffersSearchOnTheKeyboard() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceListContent(
                    state = WorkspaceListUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SEARCH_TEST_TAG).assert(
            SemanticsMatcher.expectValue(SemanticsProperties.ImeAction, ImeAction.Search),
        )
    }

    @Test
    fun nothingAtAllAndNothingMatchingAreDifferentSentences() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceListContent(
                    state = WorkspaceListUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(LIST_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(LIST_LOADING_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLoadingListShowsSkeletonsInsteadOfAnEmptySentence() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                WorkspaceListContent(
                    state = WorkspaceListUiState(loading = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(LIST_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithContentDescription(
            context.getString(R.string.taffy_workspace_list_loading),
        ).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(LIST_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun theFullWorkspaceRosterComposesRowsOnlyAsTheyEnterTheViewport() {
        val prototype = WorkspacePreviewStates.list.workspaces.first()
        val workspaces = List(32) { index ->
            prototype.copy(
                id = WorkspaceId("workspace-$index"),
                displayName = "Workspace $index",
            )
        }
        val lastTag = "$WORKSPACE_TEST_TAG_PREFIX${workspaces.last().id.value}"
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceListContent(
                    state = WorkspaceListUiState(
                        workspaces = workspaces,
                        totalCount = workspaces.size,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(lastTag).assertDoesNotExist()
        compose.onNodeWithTag(LIST_TEST_TAG).performScrollToNode(hasTestTag(lastTag))
        compose.onNodeWithTag(lastTag).assertExists()
    }

    @Test
    fun newWorkspaceAsksToStartOne() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceListContent(
                    state = WorkspaceListUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(CREATE_TEST_TAG).performClick()

        assertEquals(listOf<WorkspaceListIntent>(WorkspaceListIntent.Create), intents)
    }

    /**
     * The list names the profile it belongs to only when the state carries one,
     * which the view model supplies once the device has more than one profile.
     */
    @Test
    fun theProfileLineNamesTheProfileTheWorkspacesAreKeptIn() {
        val line = context.getString(R.string.taffy_workspace_list_profile, "Work")
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceListContent(
                    state = WorkspacePreviewStates.list.copy(profileName = "Work"),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(line).assertExists()
    }
}
