// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-309 — the export sheet.
 *
 * The preview is the file's own first lines, so the sheet shows what will be
 * written instead of describing it. The chosen format is not offered again as
 * something to choose.
 */
class ExportSheetSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<ExportSheetIntent>()

    @Test
    fun theSheetShowsTheFileItWouldWrite() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                ExportSheetContent(
                    state = WorkspacePreviewStates.exportSheet,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(PREVIEW_TEST_TAG).assertExists()
        compose.onNodeWithText(
            "# compare prices across two listings\n\nState: partly_done\n",
        ).assertExists()
        compose.onNodeWithTag(DESTINATION_TEST_TAG).assertExists()
    }

    @Test
    fun theFormatAlreadyChosenIsNotOfferedAgain() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                ExportSheetContent(
                    state = WorkspacePreviewStates.exportSheet,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$FORMAT_TEST_TAG_PREFIX${ExportFormat.MARKDOWN.label}")
            .assertIsNotEnabled()
        compose.onNodeWithTag("$FORMAT_TEST_TAG_PREFIX${ExportFormat.COMMA_SEPARATED.label}")
            .assertIsEnabled()
    }

    @Test
    fun choosingTheOtherFormatSendsThatFormat() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                ExportSheetContent(
                    state = WorkspacePreviewStates.exportSheet,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$FORMAT_TEST_TAG_PREFIX${ExportFormat.COMMA_SEPARATED.label}")
            .performClick()

        assertEquals(
            listOf(ExportSheetIntent.Select(ExportFormat.COMMA_SEPARATED)),
            intents,
        )
    }

    @Test
    fun thereIsNothingToWriteWhenThereIsNoPreview() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                ExportSheetContent(
                    state = ExportSheetUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EXPORT_TEST_TAG).assertIsNotEnabled()
    }

    @Test
    fun aWorkspaceThatNoLongerResolvesSaysSoRatherThanOfferingAnEmptyFile() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                ExportSheetContent(
                    state = ExportSheetUiState(missing = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(EXPORT_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(EXPORT_LOADING_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLoadingSheetShowsSkeletonsInsteadOfGone() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                ExportSheetContent(
                    state = ExportSheetUiState(loading = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EXPORT_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(EXPORT_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aTerminalWriteResultIsVisibleAndAllowsRetry() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                ExportSheetContent(
                    state = WorkspacePreviewStates.exportSheet.copy(
                        exportStatus = ExportStatus.FAILED,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EXPORT_STATUS_TEST_TAG).assertExists()
        compose.onNodeWithText("The file couldn’t be saved. Try again.").assertExists()
        compose.onNodeWithTag(EXPORT_TEST_TAG).assertIsEnabled()
    }
}
