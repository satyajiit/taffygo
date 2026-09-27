// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import android.content.Context
import androidx.compose.ui.test.assertContentDescriptionContains
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTextReplacement
import androidx.test.core.app.ApplicationProvider
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** SCR-203 semantics: truthful collection state and only live, person-triggered actions. */
class DownloadsSemanticsTest {
    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<DownloadsIntent>()
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun organizerControlsStatusAndRowsAreNamedForAssistiveTechnology() {
        show(DownloadPreviewStates.organizer)

        compose.onNodeWithTag(TaffyDestination.Downloads.screenId).assertExists()
        compose.onNodeWithTag(DOWNLOAD_SEARCH_TEST_TAG).assertExists()
        compose.onNodeWithTag(DOWNLOAD_FILTER_TEST_TAG).assertExists()
        compose.onNodeWithTag(DOWNLOAD_SORT_TEST_TAG).assertExists()
        compose.onNodeWithTag(DOWNLOAD_GROUPING_TEST_TAG).assertExists()
        compose.onNodeWithTag(DOWNLOAD_STATUS_TEST_TAG).assertExists()
        scrollTo("${DOWNLOAD_TEST_TAG_PREFIX}dl_1")
        compose.onNodeWithTag("${DOWNLOAD_TEST_TAG_PREFIX}dl_1")
            .assertContentDescriptionContains("retention-policy.pdf", substring = true)
            .assertContentDescriptionContains("docs.example.test", substring = true)
    }

    @Test
    fun searchChangesOnlyTheOrganizerQuery() {
        show(DownloadPreviewStates.organizer)

        compose.onNodeWithTag(DOWNLOAD_SEARCH_TEST_TAG).performTextReplacement("policy")

        assertEquals(listOf(DownloadsIntent.SetQuery("policy")), intents)
    }

    @Test
    fun eachRowAdvertisesOnlyActionsTheBrowserCurrentlyAllows() {
        show(DownloadPreviewStates.organizer)

        scrollTo("${PAUSE_TEST_TAG_PREFIX}dl_2")
        compose.onNodeWithTag("${PAUSE_TEST_TAG_PREFIX}dl_2").assertHasClickAction()
        compose.onNodeWithTag("${CANCEL_TEST_TAG_PREFIX}dl_2").assertHasClickAction()
        compose.onNodeWithTag("${OPEN_TEST_TAG_PREFIX}dl_2").assertDoesNotExist()
        compose.onNodeWithTag("${SHARE_TEST_TAG_PREFIX}dl_2").assertDoesNotExist()
        compose.onNodeWithTag("${REMOVE_TEST_TAG_PREFIX}dl_2").assertDoesNotExist()

        scrollTo("${RESUME_TEST_TAG_PREFIX}dl_3")
        compose.onNodeWithTag("${RESUME_TEST_TAG_PREFIX}dl_3").assertHasClickAction()
        compose.onNodeWithTag("${PAUSE_TEST_TAG_PREFIX}dl_3").assertDoesNotExist()

        scrollTo("${OPEN_TEST_TAG_PREFIX}dl_1")
        compose.onNodeWithTag("${OPEN_TEST_TAG_PREFIX}dl_1")
            .assertHasClickAction()
            .assertContentDescriptionContains("retention-policy.pdf", substring = true)
        compose.onNodeWithTag("${SHARE_TEST_TAG_PREFIX}dl_1").assertHasClickAction()
        compose.onNodeWithTag("${REMOVE_TEST_TAG_PREFIX}dl_1").assertHasClickAction()
        compose.onNodeWithTag("${PAUSE_TEST_TAG_PREFIX}dl_1").assertDoesNotExist()
        compose.onNodeWithTag("${CANCEL_TEST_TAG_PREFIX}dl_1").assertDoesNotExist()
    }

    @Test
    fun pauseAndResumeEmitDistinctTypedControlsForTheExactRows() {
        show(DownloadPreviewStates.organizer)

        scrollTo("${PAUSE_TEST_TAG_PREFIX}dl_2")
        compose.onNodeWithTag("${PAUSE_TEST_TAG_PREFIX}dl_2").performClick()
        scrollTo("${RESUME_TEST_TAG_PREFIX}dl_3")
        compose.onNodeWithTag("${RESUME_TEST_TAG_PREFIX}dl_3").performClick()

        assertEquals(
            listOf(
                DownloadsIntent.Pause(DownloadId("dl_2")),
                DownloadsIntent.Resume(DownloadId("dl_3")),
            ),
            intents,
        )
    }

    @Test
    fun openAndShareAreDirectButRemoveRequiresExplicitConfirmation() {
        show(DownloadPreviewStates.organizer)

        scrollTo("${OPEN_TEST_TAG_PREFIX}dl_1")
        compose.onNodeWithTag("${OPEN_TEST_TAG_PREFIX}dl_1").performClick()
        scrollTo("${SHARE_TEST_TAG_PREFIX}dl_1")
        compose.onNodeWithTag("${SHARE_TEST_TAG_PREFIX}dl_1").performClick()
        scrollTo("${REMOVE_TEST_TAG_PREFIX}dl_1")
        compose.onNodeWithTag("${REMOVE_TEST_TAG_PREFIX}dl_1").performClick()

        assertEquals(
            listOf(
                DownloadsIntent.Open(DownloadId("dl_1")),
                DownloadsIntent.Share(DownloadId("dl_1")),
            ),
            intents,
        )
        compose.onNodeWithTag(DOWNLOAD_CONFIRM_TEST_TAG).assertExists().performClick()
        assertEquals(DownloadsIntent.Remove(DownloadId("dl_1")), intents.last())
    }

    @Test
    fun cancelRequiresConfirmationAndNeverPretendsToRetry() {
        show(DownloadPreviewStates.organizer)

        scrollTo("${CANCEL_TEST_TAG_PREFIX}dl_2")
        compose.onNodeWithTag("${CANCEL_TEST_TAG_PREFIX}dl_2").performClick()
        assertEquals(emptyList<DownloadsIntent>(), intents)

        compose.onNodeWithTag(DOWNLOAD_CONFIRM_TEST_TAG).performClick()
        assertEquals(listOf(DownloadsIntent.Cancel(DownloadId("dl_2"))), intents)
        compose.onNodeWithTag("download_retry_dl_2").assertDoesNotExist()
    }

    @Test
    fun loadingAndNoMatchesRemainDistinctVisibleStates() {
        show(DownloadsUiState())
        compose.onNodeWithTag(DOWNLOAD_STATUS_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
    }

    @Test
    fun noMatchesIsVisibleAfterTheProfileSnapshotFinishes() {
        show(
            DownloadsUiState(
                query = "missing",
                collectionStatus = DownloadCollectionStatus.COMPLETE,
                totalCount = 3,
            ),
        )
        compose.onNodeWithTag(DOWNLOAD_STATUS_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
    }

    @Test
    fun providerFailureIsVisibleAndNeverPresentedAsAnEmptyProfile() {
        show(
            DownloadsUiState(
                collectionStatus = DownloadCollectionStatus.UNAVAILABLE,
            ),
        )

        compose.onNodeWithTag(DOWNLOAD_STATUS_TEST_TAG)
            .assertTextContains(context.getString(R.string.taffy_downloads_status_unavailable))
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
    }

    private fun show(state: DownloadsUiState) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                DownloadsContent(state = state, onIntent = { intents += it })
            }
        }
    }

    private fun scrollTo(tag: String) {
        compose.onNodeWithTag(DOWNLOADS_LIST_TEST_TAG).performScrollToNode(hasTestTag(tag))
    }
}
