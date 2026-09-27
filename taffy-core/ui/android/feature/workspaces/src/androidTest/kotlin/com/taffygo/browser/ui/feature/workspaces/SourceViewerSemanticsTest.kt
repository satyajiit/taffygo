// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import android.text.format.DateUtils
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.R as UiR
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-306 — the page a fact came from.
 *
 * A fact is only as good as when and how the page was read, so the capture time
 * and the extraction method are on the screen rather than implied. Each pair is
 * announced as one thing.
 */
class SourceViewerSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<SourceViewerIntent>()

    @Test
    fun theHostTheFactCountAndTheMethodAreEachOneAnnouncement() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SourceViewerContent(
                    state = WorkspacePreviewStates.sourceViewer,
                    onIntent = { intents += it },
                )
            }
        }

        listOf(
            context.getString(R.string.taffy_source_viewer_host) to "docs.example.test",
            context.getString(R.string.taffy_source_viewer_read) to
                DateUtils.getRelativeTimeSpanString(
                    WorkspacePreviewStates.sourceViewer.readAtEpochMillis,
                ).toString(),
            context.getString(R.string.taffy_source_viewer_facts) to "2",
            context.getString(R.string.taffy_source_viewer_method) to
                context.getString(R.string.taffy_source_viewer_method_value),
        ).forEach { (label, value) ->
            compose.onNodeWithContentDescription(
                context.getString(UiR.string.taffy_accessible_pair, label, value),
            ).assertExists()
        }
    }

    @Test
    fun aSourceStillInScopeSaysSoInWords() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SourceViewerContent(
                    state = WorkspacePreviewStates.sourceViewer,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithContentDescription(
            context.getString(
                UiR.string.taffy_accessible_pair,
                context.getString(R.string.taffy_source_viewer_in_scope),
                context.getString(R.string.taffy_source_viewer_included),
            ),
        ).assertExists()
    }

    @Test
    fun anExcludedSourceSaysThatInsteadOfDisappearing() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                SourceViewerContent(
                    state = WorkspacePreviewStates.sourceViewer.copy(excluded = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithContentDescription(
            context.getString(
                UiR.string.taffy_accessible_pair,
                context.getString(R.string.taffy_source_viewer_in_scope),
                context.getString(R.string.taffy_source_viewer_excluded),
            ),
        ).assertExists()
    }

    @Test
    fun theLivePageIsOneStepAway() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SourceViewerContent(
                    state = WorkspacePreviewStates.sourceViewer,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(OPEN_LIVE_TEST_TAG).assertExists().performClick()

        assertEquals(listOf(SourceViewerIntent.OpenLivePage), intents)
    }

    @Test
    fun aSourceThatNoLongerResolvesSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SourceViewerContent(
                    state = SourceViewerUiState(missing = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(OPEN_LIVE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SOURCE_LOADING_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLoadingSourceShowsSkeletonsInsteadOfGone() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                SourceViewerContent(
                    state = SourceViewerUiState(loading = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SOURCE_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(OPEN_LIVE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun theTitleOfThePageIsTheTitleOfTheScreen() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SourceViewerContent(
                    state = WorkspacePreviewStates.sourceViewer,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithContentDescription(
            context.getString(
                UiR.string.taffy_accessible_pair,
                context.getString(R.string.taffy_source_viewer_host),
                "docs.example.test",
            ),
        ).assertContentDescriptionEquals(
            context.getString(
                UiR.string.taffy_accessible_pair,
                context.getString(R.string.taffy_source_viewer_host),
                "docs.example.test",
            ),
        )
    }
}
