// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartPurpose
import com.taffygo.browser.ui.core.model.TaffyPartsState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DownloadsReducerTest {
    @Test
    fun liveReadingMovesAPartForwardButNeverBackwardOrAcrossVersions() {
        val forward = projectDownloads(
            organizer = emptyOrganizer(),
            parts = partsState(part(downloadedBytes = 100)),
            liveProgress = mapOf(PART_ID to progress(downloadedBytes = 400)),
        )
        val backward = projectDownloads(
            organizer = emptyOrganizer(),
            parts = partsState(part(downloadedBytes = 900)),
            liveProgress = mapOf(PART_ID to progress(downloadedBytes = 400)),
        )
        val otherVersion = projectDownloads(
            organizer = emptyOrganizer(),
            parts = partsState(part(downloadedBytes = 100)),
            liveProgress = mapOf(PART_ID to progress(downloadedBytes = 900, version = "2")),
        )

        assertEquals(400L, forward.parts.single().downloadedBytes)
        assertEquals(900L, backward.parts.single().downloadedBytes)
        assertEquals(100L, otherVersion.parts.single().downloadedBytes)
    }

    @Test
    fun holdsAndUnknownTotalsRemainHonest() {
        val defect = part(0).copy(hold = TaffyPartHold.PRODUCT_DEFECT)
        val unpublished = part(0).copy(
            totalBytes = 0,
            availability = TaffyPartAvailability.MISSING,
        )

        assertFalse(defect.canRetry)
        assertEquals(R.string.taffy_parts_hold_product_defect, statusName(defect))
        assertNull(unpublished.fraction)
        assertEquals(R.string.taffy_parts_state_missing, statusName(unpublished))
    }

    @Test
    fun collectionStatesDistinguishLoadingEmptyAndNoMatches() {
        val loading = DownloadsUiState()
        val empty = loading.copy(collectionStatus = DownloadCollectionStatus.COMPLETE)
        val noMatches = empty.copy(totalCount = 2, visibleCount = 0)
        val unavailable = loading.copy(collectionStatus = DownloadCollectionStatus.UNAVAILABLE)

        assertTrue(loading.isLoading)
        assertTrue(empty.hasNoDownloads)
        assertTrue(noMatches.hasNoMatches)
        assertTrue(unavailable.isUnavailable)
        assertFalse(unavailable.hasNoDownloads)
    }

    private fun emptyOrganizer() = ProjectedDownloadCollection(
        query = "",
        filter = DownloadFilter.ALL,
        sort = DownloadSort.NEWEST,
        grouping = DownloadGrouping.NONE,
        status = DownloadCollectionStatus.COMPLETE,
        projection = DownloadOrganizerProjection(emptyList(), 0, 0),
    )

    private fun part(downloadedBytes: Long) = TaffyPart(
        id = PART_ID,
        version = "1",
        purpose = TaffyPartPurpose.PYTHON_LIBRARY,
        availability = TaffyPartAvailability.PARTIAL,
        downloadedBytes = downloadedBytes,
        totalBytes = 1_000,
        attempts = 1,
        hold = null,
    )

    private fun partsState(vararg parts: TaffyPart) = TaffyPartsState(
        supported = true,
        parts = parts.toList(),
    )

    private fun progress(downloadedBytes: Long, version: String = "1") = TaffyPartProgress(
        id = PART_ID,
        version = version,
        downloadedBytes = downloadedBytes,
        totalBytes = 1_000,
    )

    private companion object {
        val PART_ID = TaffyPartId("python-stdlib")
    }
}
