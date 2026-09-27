// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-206 — Ads and trackers.
 *
 * The week is a pair of tiles when counted. A missing week is not labelled
 * as this week from the lifetime total.
 */
class FilteringSettingsSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<FilteringSettingsIntent>()

    @Test
    fun theScreenCarriesTheToggleTheTotalAndTheEmptyExceptionWords() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.AdAndTrackerBlocking.screenId).assertExists()
        compose.onNodeWithTag(FILTERING_HERO_TEST_TAG).assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_filtering_hero_eyebrow))
            .assertExists()
        compose.onNodeWithText("Included for every account").assertDoesNotExist()
        compose.onNodeWithText("Ads and trackers stay free for every account.")
            .assertDoesNotExist()
        compose.onNodeWithTag(FILTERING_SWITCH_TEST_TAG).assertIsOn()
        compose.onNodeWithTag(FILTERING_WEEK_TEST_TAG).assertExists()
        compose.onNodeWithTag(FILTERING_SITES_TEST_TAG).assertExists()
        compose.onNodeWithTag(FILTERING_TOTAL_TEST_TAG).assertExists()
        compose.onNodeWithTag(FILTERING_WHY_TEST_TAG).assertExists()
        scrollTo(FILTERING_NO_EXCEPTIONS_TEST_TAG)
        compose.onNodeWithTag(FILTERING_NO_EXCEPTIONS_TEST_TAG).assertExists()
    }

    @Test
    fun aMissingWeekDoesNotLabelTheLifetimeAsThisWeek() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(blockedTotal = 41),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_filtering_week_unavailable))
            .assertExists()
        compose.onNodeWithTag(FILTERING_TOTAL_TEST_TAG).assertExists()
        compose.onNodeWithText("41 this week").assertDoesNotExist()
    }

    @Test
    fun aMeasuredWeekDoesNotClaimItIsUncounted() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(
                        blockedThisWeek = 84,
                        minimumSitesThisWeek = 3,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_filtering_week_unavailable))
            .assertDoesNotExist()
        compose.onNodeWithTag(FILTERING_WEEK_UNAVAILABLE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(FILTERING_WEEK_TEST_TAG).assertExists()
        compose.onNodeWithTag(FILTERING_SITES_TEST_TAG).assertExists()
    }

    @Test
    fun emptyExceptionsWhileOffDoNotClaimBlockingIsOn() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(enabled = false),
                    onIntent = { intents += it },
                )
            }
        }

        scrollTo(FILTERING_NO_EXCEPTIONS_TEST_TAG)
        compose.onNodeWithText(context.getString(R.string.taffy_filtering_no_exceptions_off))
            .assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_filtering_no_exceptions))
            .assertDoesNotExist()
    }

    @Test
    fun aDisabledProfileReportsItsSwitchOff() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(enabled = false),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(FILTERING_SWITCH_TEST_TAG).assertIsOff()
    }

    @Test
    fun togglingSendsTheOppositeOfWhatIsShown() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(FILTERING_SWITCH_TEST_TAG).performClick()

        assertEquals(listOf<FilteringSettingsIntent>(
            FilteringSettingsIntent.SetEnabled(false),
        ), intents)
    }

    /**
     * The name of this test was a promise it did not keep.
     *
     * It found the row and stopped. It could not press the button, because the
     * row's column carried `semantics(mergeDescendants = true)`, which
     * collapsed the subtree into one node whose `OnClick` was the row's rather
     * than the button's — so a click landed on the text. The description now
     * sits on the button, which is both a real control and the thing the
     * description is about.
     */
    @Test
    fun anExceptionRowSendsItsOwnHostBack() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(
                        exceptionHosts = listOf("news.example.test", "docs.example.test"),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        val button = "${FILTERING_EXCEPTION_REMOVE_TEST_TAG_PREFIX}docs.example.test"
        scrollTo(button)
        compose.onNodeWithTag(button).assertExists().assertHasClickAction().performClick()

        assertEquals(
            listOf<FilteringSettingsIntent>(
                FilteringSettingsIntent.RemoveException("docs.example.test"),
            ),
            intents,
        )
    }

    /**
     * With blocking off the rows stay and the buttons do not act.
     *
     * Hiding the rows would contradict this screen's own empty state, which
     * deliberately reports "No exceptions" while blocking is off. The list is a
     * record; only the button is a control.
     */
    @Test
    fun exceptionRowsStayReadableWhileBlockingIsOff() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(
                        enabled = false,
                        exceptionHosts = listOf("news.example.test"),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        val row = "${FILTERING_EXCEPTION_TEST_TAG_PREFIX}news.example.test"
        scrollTo(row)
        compose.onNodeWithTag(row).assertExists()
        compose.onNodeWithTag(FILTERING_EXCEPTIONS_OFF_TEST_TAG).assertExists()
        compose.onNodeWithTag("${FILTERING_EXCEPTION_REMOVE_TEST_TAG_PREFIX}news.example.test")
            .assertIsNotEnabled()
        assertTrue(intents.isEmpty())
    }

    @Test
    fun aLargeExceptionListComposesRowsOnlyAsTheyEnterTheViewport() {
        val hosts = List(200) { index -> "site-$index.example.test" }
        val lastTag = "$FILTERING_EXCEPTION_TEST_TAG_PREFIX${hosts.last()}"
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FilteringSettingsContent(
                    state = FilteringSettingsUiState(exceptionHosts = hosts),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(lastTag).assertDoesNotExist()
        scrollTo(lastTag)
        compose.onNodeWithTag(lastTag).assertExists()
    }

    private fun scrollTo(tag: String) {
        compose.onNodeWithTag(FILTERING_LIST_TEST_TAG).performScrollToNode(hasTestTag(tag))
    }
}
