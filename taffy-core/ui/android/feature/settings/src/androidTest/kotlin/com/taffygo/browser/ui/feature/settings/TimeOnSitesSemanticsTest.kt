// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/** Screen SCR-411 — no fake bars when dwell time is missing. */
class TimeOnSitesSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<TimeOnSitesIntent>()

    @Test
    fun unavailableHasCaptionsAndNoSiteRows() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TimeOnSitesContent(state = TimeOnSitesUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.TimeOnSites.screenId).assertExists()
        compose.onNodeWithTag(TIME_HERO_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_UNAVAILABLE_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_EMPTY_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_LIST_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_TOTAL_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_LOADING_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(
            "$TIME_RANGE_TEST_TAG_PREFIX${TimeOnSitesUiState.Range.TODAY.name}",
        ).assertIsSelected()
        compose.onNodeWithTag(TIME_SCROLL_TEST_TAG).performScrollToIndex(5)
        compose.onNodeWithTag(TIME_CAPTION_PRIVATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_CAPTION_TAFFY_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_RETENTION_TEST_TAG).assertExists()
    }

    @Test
    fun emptyIsReadyWithNoRowsNotTheUnavailableCard() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TimeOnSitesContent(
                    state = TimeOnSitesUiState(availability = YouSurfaceAvailability.READY),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TIME_HERO_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_SCROLL_TEST_TAG).performScrollToIndex(4)
        compose.onNodeWithTag(TIME_EMPTY_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_UNAVAILABLE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_LIST_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_TOTAL_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_LOADING_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun loadingShowsSkeletonsNotRows() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TimeOnSitesContent(
                    state = TimeOnSitesUiState(availability = YouSurfaceAvailability.LOADING),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TIME_HERO_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_LIST_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_EMPTY_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_UNAVAILABLE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_TOTAL_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(
            "$TIME_RANGE_TEST_TAG_PREFIX${TimeOnSitesUiState.Range.TODAY.name}",
        ).assertDoesNotExist()
    }

    @Test
    fun choosingThisWeekSendsTheRange() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TimeOnSitesContent(state = TimeOnSitesUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TIME_SCROLL_TEST_TAG).performScrollToIndex(2)
        compose.onNodeWithTag(
            "$TIME_RANGE_TEST_TAG_PREFIX${TimeOnSitesUiState.Range.THIS_WEEK.name}",
        ).performClick()
        assertEquals(
            listOf(TimeOnSitesIntent.SelectRange(TimeOnSitesUiState.Range.THIS_WEEK)),
            intents,
        )
    }

    @Test
    fun readyRowsNameTheDurationInText() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                TimeOnSitesContent(
                    state = TimeOnSitesUiState(
                        availability = YouSurfaceAvailability.READY,
                        sites = listOf(TimeOnSitesUiState.Site("croma.com", 22L * 60_000)),
                        totalMillis = 22L * 60_000,
                        largestMillis = 22L * 60_000,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TIME_HERO_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_TOTAL_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_LIST_TEST_TAG).performScrollToIndex(4)
        compose.onNodeWithTag("${TIME_SITE_TEST_TAG_PREFIX}croma.com").assertExists()
        compose.onNodeWithTag(TIME_EMPTY_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TIME_UNAVAILABLE_TEST_TAG).assertDoesNotExist()
        assertTrue(intents.isEmpty())
    }

    @Test
    fun groupedAndRecoveredTimeHaveExplicitNotes() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TimeOnSitesContent(
                    state = TimeOnSitesUiState(
                        availability = YouSurfaceAvailability.READY,
                        sites = listOf(TimeOnSitesUiState.Site("", 60_000L, grouped = true)),
                        totalMillis = 60_000L,
                        largestMillis = 60_000L,
                        hasGroupedSites = true,
                        recoveredFromCorruption = true,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TIME_LIST_TEST_TAG).performScrollToIndex(4)
        compose.onNodeWithTag("${TIME_SITE_TEST_TAG_PREFIX}other").assertExists()
        compose.onNodeWithTag(TIME_LIST_TEST_TAG).performScrollToIndex(5)
        compose.onNodeWithTag(TIME_GROUPED_TEST_TAG).assertExists()
        compose.onNodeWithTag(TIME_RECOVERED_TEST_TAG).assertExists()
    }

    @Test
    fun aLargeDwellHistoryComposesOnlyVisibleSiteRows() {
        val sites = (0 until 1_000).map { index ->
            TimeOnSitesUiState.Site(
                site = "site-$index.example.test",
                durationMillis = (index + 1L) * 60_000L,
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TimeOnSitesContent(
                    state = TimeOnSitesUiState(
                        availability = YouSurfaceAvailability.READY,
                        sites = sites,
                        totalMillis = sites.sumOf(TimeOnSitesUiState.Site::durationMillis),
                        largestMillis = sites.last().durationMillis,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${TIME_SITE_TEST_TAG_PREFIX}site-999.example.test")
            .assertDoesNotExist()
        // Hero, counts heading, stats, sites heading, then one row per site.
        compose.onNodeWithTag(TIME_LIST_TEST_TAG).performScrollToIndex(sites.size + 3)
        compose.onNodeWithTag("${TIME_SITE_TEST_TAG_PREFIX}site-999.example.test").assertExists()
        compose.onNodeWithTag("${TIME_SITE_TEST_TAG_PREFIX}site-0.example.test")
            .assertDoesNotExist()
    }
}
