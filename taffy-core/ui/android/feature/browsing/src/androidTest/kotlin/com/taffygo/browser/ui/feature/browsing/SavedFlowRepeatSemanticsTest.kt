// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.onRoot
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import com.taffygo.browser.ui.core.model.SavedFlowReview
import com.taffygo.browser.ui.core.ui.SAVED_FLOW_ADDRESS_TEST_TAG
import com.taffygo.browser.ui.core.ui.SAVED_FLOW_REVIEW_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TAFFY_SAVED_FLOW_SCENE_TEST_TAG
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class SavedFlowRepeatSemanticsTest {
    @get:Rule val compose = createComposeRule()
    private val flow = SavedFlowReview(
        "document", 1u, "https://identity.example.test", "https://identity.example.test/download",
        listOf(
            SavedFlowReview.Step(SavedFlowReview.Action.OPEN_PAGE, address = "https://identity.example.test/download"),
            SavedFlowReview.Step(SavedFlowReview.Action.HANDOVER, personPurpose = SavedFlowReview.PersonPurpose.IDENTITY_NUMBER),
            SavedFlowReview.Step(SavedFlowReview.Action.HANDOVER, personPurpose = SavedFlowReview.PersonPurpose.VERIFICATION),
            SavedFlowReview.Step(SavedFlowReview.Action.DOWNLOAD, target = SavedFlowReview.Target.DOWNLOAD),
        ),
    )

    @Test fun fullReviewPrecedesManualOpenAndAnOpeningPageDisablesASecondTap() {
        val state = mutableStateOf(SavedFlowRepeatState(reviews = listOf(flow)))
        val intents = mutableListOf<AddressBarIntent>()
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    SavedFlowRepeatPanel(state.value, { intents += it })
                }
            }
        }
        compose.onNodeWithTag(TAFFY_SAVED_FLOW_SCENE_TEST_TAG).assertIsDisplayed()
        if (InstrumentationRegistry.getArguments().getString("captureTaskWorkspace") == "true") {
            compose.waitForIdle()
            val context = InstrumentationRegistry.getInstrumentation().targetContext
            File(context.getExternalFilesDir(null), "repeat-saved-flow.png").outputStream().use {
                compose.onRoot().captureToImage().asAndroidBitmap().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
            }
        }
        compose.onNodeWithTag(SAVED_FLOW_REVIEW_TEST_TAG).assertIsDisplayed()
        compose.onNodeWithTag(SAVED_FLOW_ADDRESS_TEST_TAG).performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag("$SAVED_FLOW_REPEAT_OPEN_TEST_TAG-${flow.id}")
            .performScrollTo().performClick()
        assertEquals(listOf(AddressBarIntent.OpenSavedFlow(flow)), intents)
        compose.runOnIdle { state.value = state.value.copy(opening = true) }
        compose.onNodeWithTag("$SAVED_FLOW_REPEAT_OPEN_TEST_TAG-${flow.id}").assertIsNotEnabled()
    }

    @Test fun unavailableQueryShowsNoOpenControl() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                SavedFlowRepeatPanel(SavedFlowRepeatState(failed = true), {})
            }
        }
        compose.onNodeWithTag(SAVED_FLOW_REPEAT_TEST_TAG).assertIsDisplayed()
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        compose.onNodeWithText(context.getString(R.string.taffy_repeat_unavailable)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_repeat_title)).assertDoesNotExist()
        compose.onNodeWithText(context.getString(R.string.taffy_repeat_lookup_failed)).assertIsDisplayed()
        compose.onNodeWithTag(TAFFY_SAVED_FLOW_SCENE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag("$SAVED_FLOW_REPEAT_OPEN_TEST_TAG-${flow.id}").assertDoesNotExist()
    }

    @Test fun checkingDoesNotAnnounceAMatchBeforeTheLookupReturns() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                SavedFlowRepeatPanel(SavedFlowRepeatState(checking = true), {})
            }
        }
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        compose.onNodeWithText(context.getString(R.string.taffy_repeat_checking)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_repeat_title)).assertDoesNotExist()
        compose.onNodeWithText(context.getString(R.string.taffy_repeat_unavailable)).assertDoesNotExist()
    }
}
