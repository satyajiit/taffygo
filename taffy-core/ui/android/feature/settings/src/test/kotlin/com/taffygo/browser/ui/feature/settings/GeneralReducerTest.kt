// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.SearchEngineId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GeneralReducerTest {

    @Test
    fun `choosing an available engine opens the picker`() {
        val state = GeneralUiState(searchEngineAvailable = true)
        val after = reduceGeneral(state, GeneralIntent.ChooseSearchEngine)
        assertTrue(after.pickingSearchEngine)
        assertEquals(SearchEngineId.GOOGLE, after.selectedEngineId)
    }

    @Test
    fun `choosing an unavailable engine does not invent one`() {
        val state = GeneralUiState(searchEngineAvailable = false)
        assertEquals(state, reduceGeneral(state, GeneralIntent.ChooseSearchEngine))
        assertFalse(reduceGeneral(state, GeneralIntent.ChooseSearchEngine).pickingSearchEngine)
    }

    @Test
    fun `dismissing the picker returns to general`() {
        val state = GeneralUiState(pickingSearchEngine = true)
        assertFalse(reduceGeneral(state, GeneralIntent.DismissSearchEngine).pickingSearchEngine)
    }

    @Test
    fun `downloads and appearance leave the engine alone`() {
        val state = GeneralUiState(selectedEngineId = SearchEngineId.BING)
        assertEquals(state, reduceGeneral(state, GeneralIntent.ChooseDownloadLocation))
        assertEquals(state, reduceGeneral(state, GeneralIntent.OpenAppearance))
    }

    @Test
    fun `download picker accepts only a location in the live snapshot`() {
        val location = DownloadLocation(
            id = "device",
            kind = GeneralSettingsRepository.DownloadLocation.Kind.DEVICE,
        )
        val state = GeneralUiState(
            downloadLocationAvailable = true,
            downloadLocations = listOf(location),
        )
        assertTrue(reduceGeneral(state, GeneralIntent.ChooseDownloadLocation).pickingDownloadLocation)
        assertTrue(
            reduceGeneral(state, GeneralIntent.SelectDownloadLocation("device"))
                .savingDownloadLocation,
        )
        assertEquals(
            DownloadLocationFailure.SELECTION_UNAVAILABLE,
            reduceGeneral(state, GeneralIntent.SelectDownloadLocation("unknown"))
                .downloadLocationFailure,
        )
    }

    @Test
    fun `a second location write is refused while one is running`() {
        val state = GeneralUiState(
            downloadLocationAvailable = true,
            downloadLocations = listOf(
                DownloadLocation(
                    "device",
                    GeneralSettingsRepository.DownloadLocation.Kind.DEVICE,
                ),
            ),
            savingDownloadLocation = true,
        )
        assertEquals(state, reduceGeneral(state, GeneralIntent.SelectDownloadLocation("device")))
    }
}
