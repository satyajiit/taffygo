// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.graphics.toArgb
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.SavedFlowReview
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.SAVED_FLOW_ADDRESS_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TAFFY_SAVED_FLOW_SCENE_TEST_TAG
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class TaskSkillReviewSemanticsTest {
    @get:Rule val compose = createComposeRule()

    @Test fun completedTaskOpensFullReviewBeforeAnExplicitSave() {
        var accepted = 0
        var expectedBackground = 0
        compose.setContent {
            var reviewing by remember { mutableStateOf(false) }
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                expectedBackground = TaffyTheme.colors.surfaceRaised.copy(alpha = 1f).toArgb()
                TaskViewContent(
                    state = TaskViewUiState(taskId = "finished", goal = "Download my document", state = TaskDisplayState.DONE),
                    onIntent = {},
                    skillReviewState = TaskSkillReviewUiState(taskId = "finished", review = flow(), showReview = reviewing),
                    onReviewFlow = { reviewing = true },
                    onAcceptFlow = { accepted++ },
                    onCloseFlowReview = { reviewing = false },
                )
            }
        }
        compose.onNodeWithTag(TASK_FLOW_SAVE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TAFFY_SAVED_FLOW_SCENE_TEST_TAG).assertIsDisplayed()
        captureIfRequested("task-flow-offer.png")
        compose.onNodeWithTag(TASK_FLOW_REVIEW_TEST_TAG).performClick()
        compose.onNodeWithTag(SAVED_FLOW_ADDRESS_TEST_TAG).assertTextEquals(ADDRESS).assertIsDisplayed()
        // Android window animations run outside Compose's test clock.
        android.os.SystemClock.sleep(500)
        compose.waitForIdle()
        val dialog = compose.onNodeWithTag(TASK_FLOW_DIALOG_TEST_TAG).captureToImage().asAndroidBitmap()
        assertEquals(expectedBackground, dialog.getPixel(dialog.width / 30, dialog.height / 2))
        captureIfRequested("task-flow-review.png")
        compose.onNodeWithTag(TASK_FLOW_SAVE_TEST_TAG).performClick()
        assertEquals(1, accepted)
    }

    @Test fun systemTextSizeKeepsPrivateEntryAndSaveReachable() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        InstrumentationRegistry.getArguments().getString("assertSystemFontScale")?.let {
            assertEquals(it.toFloat(), context.resources.configuration.fontScale, 0.01f)
        }
        // Dialog windows read the system configuration independently of a parent's LocalDensity.
        // The accessibility run sets emulator font_scale=2.0 before starting instrumentation.
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                TaskSkillReviewDialog(
                    TaskSkillReviewUiState(taskId = "finished", review = flow(), showReview = true),
                    onAccept = {}, onClose = {},
                )
            }
        }
        compose.onNodeWithTag(SAVED_FLOW_ADDRESS_TEST_TAG).performScrollTo()
            .assertTextEquals(ADDRESS).assertIsDisplayed()
        compose.onNodeWithText(context.getString(com.taffygo.browser.ui.core.ui.R.string.taffy_flow_person_verification))
            .performScrollTo().assertIsDisplayed()
        compose.onNodeWithText(context.getString(com.taffygo.browser.ui.core.ui.R.string.taffy_flow_review_privacy))
            .performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag(TASK_FLOW_SAVE_TEST_TAG).assertIsDisplayed()
        captureIfRequested("task-flow-review-large-font.png")
    }

    @Test fun aDifferentTasksPendingOfferIsAbsent() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = TaskViewUiState(taskId = "new-task", goal = "Another errand", state = TaskDisplayState.RUNNING),
                    onIntent = {},
                    skillReviewState = TaskSkillReviewUiState(taskId = "finished", review = flow()),
                )
            }
        }
        compose.onNodeWithTag(TASK_FLOW_OFFER_TEST_TAG).assertDoesNotExist()
    }

    private fun flow() = SavedFlowReview(
        id = "download-document", version = 1u, origin = "https://identity.example.test",
        startingAddress = ADDRESS,
        steps = listOf(
            SavedFlowReview.Step(SavedFlowReview.Action.OPEN_PAGE, address = ADDRESS),
            SavedFlowReview.Step(SavedFlowReview.Action.HANDOVER, personPurpose = SavedFlowReview.PersonPurpose.IDENTITY_NUMBER),
            SavedFlowReview.Step(SavedFlowReview.Action.HANDOVER, personPurpose = SavedFlowReview.PersonPurpose.VERIFICATION),
            SavedFlowReview.Step(SavedFlowReview.Action.CHOOSE_CONTROL, target = SavedFlowReview.Target.DOWNLOAD),
        ),
    )

    private fun captureIfRequested(name: String) {
        if (InstrumentationRegistry.getArguments().getString("captureTaskWorkspace") != "true") return
        android.os.SystemClock.sleep(500)
        compose.waitForIdle()
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        File(context.getExternalFilesDir(null), name).outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
        }
    }

    private companion object {
        const val ADDRESS = "https://identity.example.test/download-document"
    }
}
