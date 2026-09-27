// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-204 — the site sheet for ad and tracker blocking.
 *
 * The toggle's meaning is the one-tap exception: on means blocking acts on
 * this site, off records the host. The master toggle being off removes the
 * site toggle rather than showing a dead switch.
 */
class SiteFilteringSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<BrowserMainIntent>()

    private fun show(state: SiteFilteringUiState) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                SiteFilteringSheet(state = state, onIntent = { intents += it })
            }
        }
    }

    @Test
    fun anActivePageShowsItsCountAndAnOnToggle() {
        show(
            SiteFilteringUiState(
                host = "news.example.test",
                filteringActive = true,
                blockedCount = 12,
            ),
        )

        compose.onNodeWithTag(SITE_FILTERING_TEST_TAG).assertExists()
        compose.onNodeWithTag(SITE_FILTERING_STATUS_TEST_TAG).assertExists()
        compose.onNodeWithTag(SITE_FILTERING_TOGGLE_TEST_TAG).assertExists()
    }

    @Test
    fun anExceptedSiteAsksToBlockAgain() {
        show(
            SiteFilteringUiState(
                host = "news.example.test",
                excepted = true,
            ),
        )

        compose.onNodeWithTag(SITE_FILTERING_TOGGLE_TEST_TAG).performClick()

        assertEquals(
            listOf<BrowserMainIntent>(BrowserMainIntent.SetSiteBlocking(true)),
            intents,
        )
    }

    @Test
    fun aBlockedSiteAsksToStopBlockingHere() {
        show(SiteFilteringUiState(host = "news.example.test"))

        compose.onNodeWithTag(SITE_FILTERING_TOGGLE_TEST_TAG).performClick()

        assertEquals(
            listOf<BrowserMainIntent>(BrowserMainIntent.SetSiteBlocking(false)),
            intents,
        )
    }

    @Test
    fun theMasterToggleOffRemovesTheSiteToggleInsteadOfDeadeningIt() {
        show(SiteFilteringUiState(host = "news.example.test", enabled = false))

        compose.onNodeWithTag(SITE_FILTERING_TOGGLE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SITE_FILTERING_SETTINGS_TEST_TAG).assertExists()
    }

    @Test
    fun theExpandedSheetShowsWhyConnectionPermissionsAndDesktop() {
        show(
            SiteFilteringUiState(
                host = "news.example.test",
                filteringActive = true,
                blockedCount = 12,
                isSecure = true,
            ),
        )

        compose.onNodeWithTag(SITE_FILTERING_WHY_TEST_TAG).assertExists()
        compose.onNodeWithTag(SITE_FILTERING_CONNECTION_TEST_TAG).assertExists()
        compose.onNodeWithTag(SITE_FILTERING_PERMISSIONS_TEST_TAG).assertExists()
        compose.onNodeWithTag(SITE_PERMISSIONS_UNAVAILABLE_TEST_TAG).assertExists()
        compose.onNodeWithTag(PAGE_ZOOM_TEST_TAG).assertExists()
        compose.onNodeWithTag(SITE_FILTERING_DESKTOP_TEST_TAG).assertExists()
    }

    @Test
    fun loadingAndEmptyPermissionsHaveDifferentAccessibleAnswers() {
        var state by androidx.compose.runtime.mutableStateOf(
            SiteFilteringUiState(
                host = "news.example.test",
                permissions = SiteInfoRepository.PermissionState.Loading,
            ),
        )
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                SiteFilteringSheet(state = state, onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(SITE_PERMISSIONS_LOADING_TEST_TAG).assertExists()
        compose.runOnUiThread {
            state = state.copy(permissions = SiteInfoRepository.PermissionState.Empty)
        }
        compose.onNodeWithTag(SITE_PERMISSIONS_LOADING_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SITE_PERMISSIONS_EMPTY_TEST_TAG).assertExists()
    }

    @Test
    fun changedPermissionsNameKnownCapabilitiesCountOthersAndResetExplicitly() {
        show(
            SiteFilteringUiState(
                host = "news.example.test",
                permissions = SiteInfoRepository.PermissionState.Changed(
                    changedCount = 3,
                    capabilities = listOf(
                        SiteInfoRepository.PermissionCapability.CAMERA,
                        SiteInfoRepository.PermissionCapability.MICROPHONE,
                    ),
                ),
            ),
        )

        compose.onNodeWithTag(SITE_PERMISSIONS_CHANGED_TEST_TAG)
            .assertContentDescriptionEquals("3 changed permissions")
        compose.onNodeWithTag("${SITE_PERMISSION_CAPABILITY_TEST_TAG_PREFIX}camera")
            .assertContentDescriptionEquals("Camera")
        compose.onNodeWithTag("${SITE_PERMISSION_CAPABILITY_TEST_TAG_PREFIX}microphone")
            .assertContentDescriptionEquals("Microphone")
        compose.onNodeWithTag(SITE_PERMISSIONS_OTHER_TEST_TAG)
            .assertContentDescriptionEquals("1 other site permission change")
        // Scrolled to first. The sheet is a Column with verticalScroll, so every
        // child is composed whether or not it is on screen: onNodeWithTag finds
        // this button below the fold, performClick lands nowhere, and the case
        // fails with an empty intent list rather than a missing node.
        compose.onNodeWithTag(SITE_PERMISSIONS_RESET_TEST_TAG).performScrollTo().performClick()

        assertEquals(listOf(BrowserMainIntent.RequestSitePermissionReset), intents)
    }

    @Test
    fun permissionResetConfirmationNamesTheHostAndRequiresASeparateConfirm() {
        show(
            SiteFilteringUiState(
                host = "news.example.test",
                permissions = SiteInfoRepository.PermissionState.Changed(
                    changedCount = 1,
                    capabilities = listOf(SiteInfoRepository.PermissionCapability.CAMERA),
                ),
                permissionResetConfirmation = true,
            ),
        )

        compose.onNodeWithTag(SITE_PERMISSIONS_RESET_CONFIRMATION_TEST_TAG).assertExists()
        compose.onNodeWithText(
            "Remove every saved permission choice for news.example.test. " +
                "The site will use your defaults and can ask again.",
        ).assertExists()
        compose.onNodeWithTag("${SITE_PERMISSIONS_RESET_CONFIRMATION_TEST_TAG}_confirm")
            .performClick()

        assertEquals(listOf(BrowserMainIntent.ConfirmSitePermissionReset), intents)
    }

    @Test
    fun aResetInProgressIsNamedAndCannotBeSentTwice() {
        show(
            SiteFilteringUiState(
                host = "news.example.test",
                permissions = SiteInfoRepository.PermissionState.Changed(
                    changedCount = 1,
                    capabilities = listOf(SiteInfoRepository.PermissionCapability.LOCATION),
                ),
                permissionReset = SiteFilteringUiState.ActionProgress.RUNNING,
            ),
        )

        compose.onNodeWithTag(SITE_PERMISSIONS_RESET_STATUS_TEST_TAG)
            .assertContentDescriptionEquals("Resetting changed permissions…")
        compose.onNodeWithTag(SITE_PERMISSIONS_RESET_TEST_TAG)
            .assertIsNotEnabled()

        assertEquals(emptyList<BrowserMainIntent>(), intents)
    }

    @Test
    fun aFailedResetKeepsTheFactsAndNamesTheFailure() {
        show(
            SiteFilteringUiState(
                host = "news.example.test",
                permissions = SiteInfoRepository.PermissionState.Changed(
                    changedCount = 1,
                    capabilities = listOf(SiteInfoRepository.PermissionCapability.LOCATION),
                ),
                permissionReset = SiteFilteringUiState.ActionProgress.FAILED,
            ),
        )

        compose.onNodeWithTag(SITE_PERMISSIONS_CHANGED_TEST_TAG).assertExists()
        compose.onNodeWithTag(SITE_PERMISSIONS_RESET_STATUS_TEST_TAG)
            .assertContentDescriptionEquals(
                "Changed permissions couldn't be reset. The current choices are shown above.",
            )
        compose.onNodeWithTag(SITE_PERMISSIONS_RESET_TEST_TAG).assertExists()
    }

    @Test
    fun pageZoomExposesBoundedActionsAndTheCurrentLevel() {
        show(
            SiteFilteringUiState(
                host = "news.example.test",
                pageZoom = PageZoomState(
                    available = true,
                    percent = 125,
                    canZoomOut = true,
                    canZoomIn = true,
                    canReset = true,
                ),
            ),
        )

        compose.onNodeWithTag(PAGE_ZOOM_OUT_TEST_TAG).performClick()
        compose.onNodeWithTag(PAGE_ZOOM_RESET_TEST_TAG).performClick()
        compose.onNodeWithTag(PAGE_ZOOM_IN_TEST_TAG).performClick()

        assertEquals(
            listOf(
                BrowserMainIntent.ZoomPageOut,
                BrowserMainIntent.ResetPageZoom,
                BrowserMainIntent.ZoomPageIn,
            ),
            intents,
        )
    }

    @Test
    fun theSettingsRowOpensTheBlockingSettings() {
        show(SiteFilteringUiState(host = "news.example.test"))

        // Scrolled to first, and it is the last row in the sheet, so on a phone
        // viewport it is always below the fold. See the note on the permission
        // reset button above.
        compose.onNodeWithTag(SITE_FILTERING_SETTINGS_TEST_TAG).performScrollTo().performClick()

        assertEquals(
            listOf<BrowserMainIntent>(BrowserMainIntent.OpenFilteringSettings),
            intents,
        )
    }
}
