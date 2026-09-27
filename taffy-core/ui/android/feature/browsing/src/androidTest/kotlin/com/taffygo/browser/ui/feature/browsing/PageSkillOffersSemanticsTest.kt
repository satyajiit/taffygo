// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.SavedFlowReview
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.savedFlowStartRequest
import com.taffygo.browser.ui.core.task.taskStartRequest
import com.taffygo.browser.ui.core.ui.SAVED_FLOW_ADDRESS_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPreview
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class PageSkillOffersSemanticsTest {
    @get:Rule val compose = createComposeRule()
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Test fun regularPageMenuOffersSavedFlowsAndPrivatePageWithholdsIt() {
        val intents = mutableListOf<BrowserMainIntent>()
        var privatePage by mutableStateOf(false)
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                BrowserMenu(BrowserMainUiState(isPrivate = privatePage), { intents += it })
            }
        }
        compose.onNodeWithTag(SAVED_FLOWS_MENU_TEST_TAG).assertIsDisplayed().performClick()
        assertEquals(listOf(BrowserMainIntent.OpenSavedFlows), intents)
        compose.runOnIdle { privatePage = true }
        compose.onNodeWithTag(SAVED_FLOWS_MENU_TEST_TAG).assertDoesNotExist()
    }

    @Test fun matchedFlowDisplaysScopeAndStartsOnlyAfterExplicitChoice() {
        val chosen = mutableListOf<PageSkillOffersUiState.Offer>()
        val offer = offer()
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                PageSkillOffersSheet(
                    PageSkillOffersUiState(open = true, host = "identity.example.test",
                        availability = PageSkillOffersUiState.Availability.READY, offers = listOf(offer)),
                    onClose = {}, onRefresh = {}, onStart = { chosen += it }, onSetup = {},
                )
            }
        }
        compose.onNodeWithTag(PAGE_FLOWS_TEST_TAG).assertIsDisplayed()
        assertEquals(emptyList<PageSkillOffersUiState.Offer>(), chosen)
        compose.onNodeWithText(context.getString(R.string.taffy_page_flows_replay)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_ask_scope_exact)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_page_flows_handover)).assertIsDisplayed()
        val providerDisclosure = context.getString(R.string.taffy_ask_disclosure_one_page, "")
        compose.onNodeWithText(providerDisclosure.trim(), substring = true).assertDoesNotExist()
        captureIfRequested("page-saved-flows.png")
        compose.onNodeWithText(context.getString(R.string.taffy_page_flows_view_steps)).performScrollTo().performClick()
        compose.onNodeWithTag(SAVED_FLOW_ADDRESS_TEST_TAG).performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag(PAGE_FLOW_START_TEST_TAG).performScrollTo().assertIsDisplayed().performClick()
        assertEquals(listOf(offer), chosen)
    }

    @Test fun authoredFlowKeepsItsProviderDisclosureAndExactPageScope() {
        val authored = offer().copy(review = null, start = taskStartRequest(
            "Read this page", TaskTemplate.BUILD_A_SOURCE_TABLE, listOf("identity.example.test"),
            TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY), CoreUiAvailability.READY, false,
        ))
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                PageSkillOffersSheet(
                    PageSkillOffersUiState(open = true, host = "identity.example.test",
                        availability = PageSkillOffersUiState.Availability.READY, offers = listOf(authored)),
                    onClose = {}, onRefresh = {}, onStart = {}, onSetup = {},
                )
            }
        }
        val providerDisclosure = context.getString(R.string.taffy_ask_disclosure_one_page, "")
        compose.onNodeWithText(providerDisclosure.trim(), substring = true).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_ask_scope_exact)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.taffy_page_flows_replay)).assertDoesNotExist()
    }

    @Test fun stalePageShowsRefreshAndCannotStartAnOldOffer() {
        var refreshed = 0
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                PageSkillOffersSheet(
                    PageSkillOffersUiState(open = true, availability = PageSkillOffersUiState.Availability.STALE),
                    onClose = {}, onRefresh = { refreshed++ }, onStart = {}, onSetup = {},
                )
            }
        }
        compose.onNodeWithTag(PAGE_FLOW_START_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PAGE_FLOWS_REFRESH_TEST_TAG).assertIsDisplayed().performClick()
        assertEquals(1, refreshed)
    }

    @Test fun missingFullStepsCanBeLoadedWithoutStartingTheFlow() {
        var managed = 0
        var loaded = 0
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                PageSkillOffersSheet(
                    PageSkillOffersUiState(open = true, availability = PageSkillOffersUiState.Availability.REVIEW_UNAVAILABLE),
                    onClose = {}, onRefresh = {}, onStart = {}, onSetup = {}, onManage = { managed++ }, onLoadReviews = { loaded++ },
                )
            }
        }
        compose.onNodeWithTag(PAGE_FLOW_START_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PAGE_FLOWS_REFRESH_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag("page_saved_flows_load_reviews").assertIsDisplayed().performClick()
        assertEquals(1, loaded)
        compose.onNodeWithTag(PAGE_FLOW_START_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PAGE_FLOWS_MANAGE_TEST_TAG).assertIsDisplayed().performClick()
        assertEquals(1, managed)
    }

    private fun offer() = PageSkillOffersUiState.Offer(
        id = "opaque-current-document", skillId = "download-document", version = 1u, stepCount = 4u,
        review = SavedFlowReview(
            "download-document", 1u, "https://identity.example.test", "https://identity.example.test/download-document",
            listOf(
                SavedFlowReview.Step(SavedFlowReview.Action.OPEN_PAGE, address = "https://identity.example.test/download-document"),
                SavedFlowReview.Step(SavedFlowReview.Action.HANDOVER, personPurpose = SavedFlowReview.PersonPurpose.IDENTITY_NUMBER),
                SavedFlowReview.Step(SavedFlowReview.Action.HANDOVER, personPurpose = SavedFlowReview.PersonPurpose.VERIFICATION),
                SavedFlowReview.Step(SavedFlowReview.Action.DOWNLOAD, target = SavedFlowReview.Target.DOWNLOAD),
            ),
        ),
        start = savedFlowStartRequest(
            "Use saved flow", "identity.example.test", CoreUiAvailability.READY, false,
        ),
    )

    private fun captureIfRequested(name: String) {
        if (InstrumentationRegistry.getArguments().getString("captureTaskWorkspace") != "true") return
        android.os.SystemClock.sleep(500)
        compose.waitForIdle()
        File(context.getExternalFilesDir(null), name).outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
        }
    }
}
