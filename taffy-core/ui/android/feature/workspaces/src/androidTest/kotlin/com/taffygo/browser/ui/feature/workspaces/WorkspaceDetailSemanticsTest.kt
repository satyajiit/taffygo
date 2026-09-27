// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.R as CoreUiR
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-305 — one workspace: its output, its sources, its conflicts.
 *
 * A conflict is a row a person can reach, not a colour on a cell. An excluded
 * source stays visible and says it is excluded, because deleting the record
 * would make the timeline dishonest.
 */
class WorkspaceDetailSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<WorkspaceDetailIntent>()

    @Test
    fun conflictsAndLostSourcesAreCountedInReachableRows() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceDetailContent(
                    state = WorkspacePreviewStates.detail,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(CONFLICT_TEST_TAG).assertContentDescriptionEquals(
            context.resources.getQuantityString(R.plurals.taffy_workspace_detail_conflicts, 1, 1),
        )
        compose.onNodeWithTag(NEEDS_SOURCE_TEST_TAG).assertContentDescriptionEquals(
            context.resources.getQuantityString(R.plurals.taffy_workspace_detail_needs_sources, 1, 1),
        )
    }

    @Test
    fun everyFactSaysWhichFieldItFillsAndHowItCameToBe() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceDetailContent(
                    state = WorkspacePreviewStates.detail,
                    onIntent = { intents += it },
                )
            }
        }

        // Status, notices, three actions, output heading, then fact rows.
        compose.onNodeWithTag(DETAIL_LIST_TEST_TAG).performScrollToIndex(6)
        // The fixture's first fact is one the sources disagree about, and the
        // conflict is what the row must say: a fact read from the page that two
        // sources contradict is not simply "from the page", and a description
        // that said so would hide the disagreement from a screen reader.
        compose.onNodeWithTag("${DETAIL_FACT_TEST_TAG_PREFIX}f1").assertContentDescriptionEquals(
            context.getString(
                R.string.taffy_workspace_detail_fact_description,
                "price",
                "62,999",
                // `:core:ui` owns the fact-kind words and this module's R is
                // non-transitive, so the label is reached through that module's
                // own R rather than through one that never held it.
                context.getString(CoreUiR.string.taffy_fact_kind_conflict),
            ),
        )
    }

    @Test
    fun anExcludedSourceStaysVisibleAndOffersNoSecondExclusion() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                WorkspaceDetailContent(
                    state = WorkspacePreviewStates.detail,
                    onIntent = { intents += it },
                )
            }
        }

        // Two facts precede the sources heading and its two rows.
        compose.onNodeWithTag(DETAIL_LIST_TEST_TAG).performScrollToIndex(9)
        compose.onNodeWithTag("${DETAIL_EXCLUDE_TEST_TAG_PREFIX}docs.example.test")
            .assertExists()
            .performClick()
        compose.onNodeWithTag(DETAIL_LIST_TEST_TAG).performScrollToIndex(10)
        compose.onNodeWithTag("${DETAIL_SOURCE_TEST_TAG_PREFIX}shop.example.test").assertExists()
        compose.onNodeWithTag("${DETAIL_EXCLUDE_TEST_TAG_PREFIX}shop.example.test")
            .assertDoesNotExist()

        assertEquals(
            listOf(WorkspaceDetailIntent.ExcludeSource(SourceId("docs.example.test"))),
            intents,
        )
    }

    @Test
    fun exportIsOneStepFromTheWorkspaceItExports() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceDetailContent(
                    state = WorkspacePreviewStates.detail,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(DETAIL_EXPORT_TEST_TAG)
            .assertExists()
            .assertHasClickAction()
            .performClick()

        assertEquals(listOf(WorkspaceDetailIntent.Export), intents)
    }

    @Test
    fun aWorkspaceThatNoLongerResolvesSaysSoRatherThanShowingAnEmptyTable() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceDetailContent(
                    state = WorkspaceDetailUiState(missing = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(DETAIL_OUTPUT_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(DETAIL_LOADING_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLoadingWorkspaceShowsSkeletonsInsteadOfGone() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                WorkspaceDetailContent(
                    state = WorkspaceDetailUiState(loading = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(DETAIL_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(DETAIL_OUTPUT_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aContractSizedWorkspaceComposesOnlyVisibleFacts() {
        val facts = (0 until 256).map { index ->
            Fact(
                id = FactId("fact_$index"),
                field = "field_$index",
                value = "value_$index",
                kind = FactKind.FROM_THE_PAGE,
                sources = emptyList(),
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WorkspaceDetailContent(
                    state = WorkspaceDetailUiState(
                        id = WorkspaceId("large_workspace"),
                        displayName = "Large workspace",
                        facts = facts,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${DETAIL_FACT_TEST_TAG_PREFIX}fact_255").assertDoesNotExist()
        // Status, three actions, output heading, then the individually keyed facts.
        compose.onNodeWithTag(DETAIL_LIST_TEST_TAG).performScrollToIndex(facts.size + 4)
        compose.onNodeWithTag("${DETAIL_FACT_TEST_TAG_PREFIX}fact_255").assertExists()
        compose.onNodeWithTag("${DETAIL_FACT_TEST_TAG_PREFIX}fact_0").assertDoesNotExist()
    }
}
