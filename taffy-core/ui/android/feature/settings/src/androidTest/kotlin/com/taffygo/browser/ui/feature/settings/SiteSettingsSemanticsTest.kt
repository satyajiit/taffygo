// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.test.performClick
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Rule
import org.junit.Assert.assertEquals
import org.junit.Test

class SiteSettingsSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun emptyReadyHasWords() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SiteSettingsContent(state = SiteSettingsUiState(), onIntent = {})
            }
        }

        compose.onNodeWithTag(TaffyDestination.SiteSettings.screenId).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
    }

    @Test
    fun unavailableHasWords() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                SiteSettingsContent(
                    state = SiteSettingsUiState(available = false),
                    onIntent = {},
                )
            }
        }

        compose.onNodeWithTag(SITE_SETTINGS_UNAVAILABLE_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SITE_SETTINGS_LIST_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLargeExceptionSetComposesOnlyTheVisibleRows() {
        val sites = List(1_000) { index ->
            SiteSettingsUiState.Site(
                host = "site-$index.example.test",
                changedPermissionCount = 1,
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SiteSettingsContent(
                    state = SiteSettingsUiState(sites = sites),
                    onIntent = {},
                )
            }
        }

        compose.onNodeWithTag(SITE_SETTINGS_LIST_TEST_TAG).assertExists()
        compose.onNodeWithTag("${SITE_SETTINGS_ROW_TEST_TAG_PREFIX}site-999.example.test")
            .assertDoesNotExist()
        compose.onNodeWithTag(SITE_SETTINGS_LIST_TEST_TAG)
            .performScrollToIndex(sites.lastIndex + SITE_SETTINGS_STATIC_LAZY_ITEMS)
        compose.onNodeWithTag("${SITE_SETTINGS_ROW_TEST_TAG_PREFIX}site-999.example.test")
            .assertExists()
        compose.onNodeWithTag("${SITE_SETTINGS_ROW_TEST_TAG_PREFIX}site-0.example.test")
            .assertDoesNotExist()
    }

    @Test
    fun aSiteRowOpensAConfirmationBeforeReset() {
        val intents = mutableListOf<SiteSettingsIntent>()
        val host = "camera.example.test"
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                var state by remember {
                    mutableStateOf(
                        SiteSettingsUiState(
                            sites = listOf(SiteSettingsUiState.Site(host, 1)),
                        ),
                    )
                }
                SiteSettingsContent(
                    state = state,
                    onIntent = { intent ->
                        intents += intent
                        state = reduceSiteSettings(state, intent)
                    },
                )
            }
        }

        compose.onNodeWithTag("$SITE_SETTINGS_ROW_TEST_TAG_PREFIX$host").performClick()
        compose.onNodeWithTag(SITE_SETTINGS_RESET_SHEET_TEST_TAG).assertExists()
        compose.onNodeWithTag(SITE_SETTINGS_RESET_CONFIRM_TEST_TAG).performClick()
        assertEquals(
            listOf(
                SiteSettingsIntent.RequestSiteReset(host),
                SiteSettingsIntent.ConfirmSiteReset,
            ),
            intents,
        )
    }

    private companion object {
        const val SITE_SETTINGS_STATIC_LAZY_ITEMS = 3
    }
}
