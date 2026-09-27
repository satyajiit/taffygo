// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performScrollToIndex
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Rule
import org.junit.Test

/** Screen SCR-412 — only the latest terminal workspace projection is shown. */
class WhatHappenedSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<WhatHappenedIntent>()

    @Test
    fun emptyLatestWorkspaceProjectionDoesNotOfferUnsupportedFilters() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WhatHappenedContent(
                    state = WhatHappenedUiState(availability = YouSurfaceAvailability.READY),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.WhatHappened.screenId).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(WHAT_EMPTY_TEST_TAG).assertExists()
        compose.onNodeWithTag(WHAT_UNAVAILABLE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun unavailableIsADifferentCardFromEmpty() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WhatHappenedContent(
                    state = WhatHappenedUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(WHAT_UNAVAILABLE_TEST_TAG).assertExists()
        compose.onNodeWithTag(WHAT_EMPTY_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun weeklyBlockingEvidenceDoesNotDependOnAnInventedTaskCount() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WhatHappenedContent(
                    state = WhatHappenedUiState(
                        availability = YouSurfaceAvailability.READY,
                        blockedRequestsThisWeek = 12L,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(WHAT_SUMMARY_TEST_TAG).assertExists()
        compose.onNodeWithTag(WHAT_UNAVAILABLE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun fiveHundredEventsComposeOnlyWhenTheyEnterTheViewport() {
        val events = (0 until 500).map { index ->
            WhatHappenedRepository.Event(
                id = "event_$index",
                kind = WhatHappenedRepository.Kind.TASK_STOPPED,
                epochMillis = index.toLong(),
                epochDay = 0L,
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                WhatHappenedContent(
                    state = WhatHappenedUiState(
                        availability = YouSurfaceAvailability.READY,
                        days = listOf(WhatHappenedUiState.Day(epochDay = 0L, events = events)),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${WHAT_EVENT_TEST_TAG_PREFIX}event_499").assertDoesNotExist()
        // Intro, day heading, then the five hundred individually keyed events.
        compose.onNodeWithTag(WHAT_LIST_TEST_TAG).performScrollToIndex(events.size + 1)
        compose.onNodeWithTag("${WHAT_EVENT_TEST_TAG_PREFIX}event_499").assertExists()
        compose.onNodeWithTag("${WHAT_EVENT_TEST_TAG_PREFIX}event_0").assertDoesNotExist()
    }
}
