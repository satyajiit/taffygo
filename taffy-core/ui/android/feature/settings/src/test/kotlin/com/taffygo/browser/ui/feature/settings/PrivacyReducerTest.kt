// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.preferences.UserPreferences
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

class PrivacyReducerTest {

    @Test
    fun `export writes only after a trusted destination was selected`() {
        val state = PrivacyUiState(exportAvailable = true)
        val choosing = reducePrivacy(state, PrivacyIntent.RequestExport)
        assertEquals(PrivacyUiState.ExportStatus.CHOOSING_DESTINATION, choosing.exportStatus)
        val writing = reducePrivacy(choosing, PrivacyIntent.ExportDestinationSelected)
        assertEquals(PrivacyUiState.ExportStatus.WRITING, writing.exportStatus)
        val finished = reducePrivacy(
            writing,
            PrivacyIntent.ExportFinished(ProfileDataControl.ExportResult.COMPLETED),
        )
        assertEquals(PrivacyUiState.ExportStatus.SUCCEEDED, finished.exportStatus)
    }

    @Test
    fun `delete requires confirmation and reports Android acceptance only as started`() {
        val state = PrivacyUiState(deleteAvailable = true)
        val confirming = reducePrivacy(state, PrivacyIntent.RequestDeleteEverything)
        assertEquals(PrivacyUiState.DeletionStatus.CONFIRMING, confirming.deletionStatus)
        val starting = reducePrivacy(confirming, PrivacyIntent.ConfirmDeleteEverything)
        assertEquals(PrivacyUiState.DeletionStatus.STARTING, starting.deletionStatus)
        val accepted = reducePrivacy(
            starting,
            PrivacyIntent.DeleteFinished(ProfileDataControl.DeletionResult.STARTED),
        )
        assertEquals(PrivacyUiState.DeletionStatus.STARTED, accepted.deletionStatus)
    }

    @Test
    fun `repository totals and preferences meet in one content-free ui state`() {
        val ui = projectPrivacyUiState(
            preferences = UserPreferences(
                providerRoute = ProviderRoute.DIRECT_WITH_YOUR_KEY,
            ),
            privacy = PrivacyCenterRepository.Snapshot(
                savedWorkspaces = PrivacyDataCount.Known(2),
                libraryItems = PrivacyDataCount.Known(3),
                memoryItems = PrivacyDataCount.Unavailable,
                connectedProviders = PrivacyDataCount.Known(1),
                recentDownloads = PrivacyDataCount.Known(256, isLowerBound = true),
                changedSites = PrivacyDataCount.Loading,
            ),
        )

        assertEquals(ProviderRoute.DIRECT_WITH_YOUR_KEY, ui.route)
        assertEquals(PrivacyDataCount.Known(2), ui.savedWorkspaces)
        assertEquals(PrivacyDataCount.Known(3), ui.libraryItems)
        assertEquals(PrivacyDataCount.Unavailable, ui.memoryItems)
        assertEquals(PrivacyDataCount.Known(1), ui.connectedProviders)
        assertEquals(PrivacyDataCount.Known(256, isLowerBound = true), ui.recentDownloads)
        assertEquals(PrivacyDataCount.Loading, ui.changedSites)
    }

    @Test
    fun `each route names its own body and none share the stored heading`() {
        assertEquals(
            R.string.taffy_privacy_route_none,
            privacyRouteBodyRes(ProviderRoute.NOT_CONFIGURED),
        )
        assertEquals(
            R.string.taffy_privacy_route_direct,
            privacyRouteBodyRes(ProviderRoute.DIRECT_WITH_YOUR_KEY),
        )
        assertEquals(
            R.string.taffy_privacy_route_no_model,
            privacyRouteBodyRes(ProviderRoute.NO_MODEL_REQUIRED),
        )
        assertNotEquals(
            R.string.taffy_privacy_stored_heading,
            R.string.taffy_privacy_hero_body,
        )
        assertNotEquals(
            R.string.taffy_settings_privacy_summary,
            R.string.taffy_privacy_hero_body,
        )
    }
}
