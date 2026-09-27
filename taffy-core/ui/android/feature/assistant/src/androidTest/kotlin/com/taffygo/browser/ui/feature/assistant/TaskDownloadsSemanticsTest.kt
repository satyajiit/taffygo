// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.TaffyPreview
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class TaskDownloadsSemanticsTest {
    @get:Rule val compose = createComposeRule()
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val file = DownloadRecord(DownloadId("owned-pdf"), "identity-document.pdf", "identity.example.test",
        153_600L, 153_600L, DownloadState.COMPLETE, setOf(DownloadAction.OPEN), "application/pdf")

    @Test fun completedPdfOffersAnExplicitOpenAtTheCurrentFontSize() {
        InstrumentationRegistry.getArguments().getString("assertSystemFontScale")?.let {
            assertEquals(it.toFloat(), context.resources.configuration.fontScale, 0.01f)
        }
        val opened = mutableListOf<DownloadId>()
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    TaskViewUiState(taskId = "finished", goal = "Download my document", state = TaskDisplayState.DONE, canSaveWorkspace = true),
                    onIntent = {}, downloadsState = TaskDownloadsUiState(taskId = "finished", files = listOf(file)),
                    onOpenDownload = { opened += it },
                )
            }
        }
        assertEquals(emptyList<DownloadId>(), opened)
        compose.onNodeWithText(context.getString(R.string.taffy_task_pdf_ready)).assertIsDisplayed()
        compose.onNodeWithText(file.fileName).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_task_view_timeline_empty_title)).assertDoesNotExist()
        compose.onNodeWithText(context.getString(R.string.taffy_task_view_output_empty_title)).assertDoesNotExist()
        compose.onNodeWithTag("$TASK_DOWNLOAD_OPEN_TEST_TAG_PREFIX${file.id.value}")
            .assertIsDisplayed()
        val large = context.resources.configuration.fontScale > 1.5f
        captureIfRequested(if (large) "task-download-pdf-large-font.png" else "task-download-pdf.png")
        compose.onNodeWithTag("$TASK_DOWNLOAD_OPEN_TEST_TAG_PREFIX${file.id.value}").performClick()
        assertEquals(listOf(file.id), opened)
    }

    @Test fun anUnknownMimeDoesNotTurnAPdfFilenameIntoAPdfClaim() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                TaskDownloadsPanel(TaskDownloadsUiState(taskId = "finished", files = listOf(file.copy(mimeType = null))),
                    onOpen = {}, onShowDownloads = {})
            }
        }
        compose.onNodeWithText(context.getString(R.string.taffy_task_file_ready)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_task_file_open)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_task_pdf_ready)).assertDoesNotExist()
    }

    @Test fun switchingTasksWithdrawsThePreviousFilesControl() {
        val task = mutableStateOf("finished")
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(TaskViewUiState(taskId = task.value, state = TaskDisplayState.DONE),
                    onIntent = {}, downloadsState = TaskDownloadsUiState(taskId = "finished", files = listOf(file)))
            }
        }
        compose.onNodeWithTag("$TASK_DOWNLOAD_OPEN_TEST_TAG_PREFIX${file.id.value}").assertIsDisplayed()
        compose.runOnIdle { task.value = "another-task" }
        compose.onNodeWithTag("$TASK_DOWNLOAD_OPEN_TEST_TAG_PREFIX${file.id.value}").assertDoesNotExist()
    }

    @Test fun refusedOpenGivesAnExplicitRouteToDownloads() {
        var openedDownloads = 0
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                TaskViewContent(
                    TaskViewUiState(taskId = "finished", goal = "Download my document", state = TaskDisplayState.DONE),
                    onIntent = {},
                    downloadsState = TaskDownloadsUiState(taskId = "finished", files = listOf(file), failed = file.id),
                    onShowDownloads = { openedDownloads++ },
                )
            }
        }
        compose.onNodeWithText(context.getString(R.string.taffy_task_file_open_failed)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_task_pdf_ready)).assertDoesNotExist()
        compose.onNodeWithText(context.getString(R.string.taffy_task_file_on_device)).assertDoesNotExist()
        compose.onNodeWithTag("$TASK_DOWNLOAD_OPEN_TEST_TAG_PREFIX${file.id.value}").assertDoesNotExist()
        captureIfRequested("task-download-refused.png")
        compose.onNodeWithTag(TASK_DOWNLOADS_VIEW_TEST_TAG).assertIsDisplayed().performClick()
        assertEquals(1, openedDownloads)
    }

    private fun captureIfRequested(name: String) {
        if (InstrumentationRegistry.getArguments().getString("captureTaskWorkspace") != "true") return
        compose.waitForIdle()
        File(context.getExternalFilesDir(null), name).outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
        }
    }
}
