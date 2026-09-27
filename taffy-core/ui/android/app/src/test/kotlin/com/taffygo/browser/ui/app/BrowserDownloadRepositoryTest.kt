// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.feature.downloads.DownloadCollectionStatus
import com.taffygo.browser.ui.feature.downloads.MAX_ORGANIZER_DOWNLOADS
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BrowserDownloadRepositoryTest {
    @Test
    fun initialQueryAndCompletenessBecomeDistinctFeatureStates() {
        assertEquals(
            DownloadCollectionStatus.LOADING,
            browserDownloadSnapshot(emptyList(), ready = false, complete = false).status,
        )
        assertEquals(
            DownloadCollectionStatus.LIMITED,
            browserDownloadSnapshot(listOf(record("one")), ready = true, complete = false).status,
        )
        assertEquals(
            DownloadCollectionStatus.COMPLETE,
            browserDownloadSnapshot(listOf(record("one")), ready = true, complete = true).status,
        )
        assertEquals(
            DownloadCollectionStatus.UNAVAILABLE,
            browserDownloadSnapshot(
                listOf(record("one")),
                ready = true,
                complete = false,
                unavailable = true,
            ).status,
        )
    }

    @Test
    fun anOversizedBrowserReadingCanNeverClaimToBeComplete() {
        val snapshot = browserDownloadSnapshot(
            downloads = (0..MAX_ORGANIZER_DOWNLOADS).map { record("$it") },
            ready = true,
            complete = true,
        )

        assertEquals(MAX_ORGANIZER_DOWNLOADS, snapshot.downloads.size)
        assertEquals(DownloadCollectionStatus.LIMITED, snapshot.status)
    }

    @Test
    fun staleOrUnadvertisedActionsFailClosedBeforeReachingChromium() {
        val downloads = listOf(record("one", setOf(DownloadAction.OPEN)))

        assertTrue(downloadActionIsCurrent(downloads, DownloadId("one"), DownloadAction.OPEN))
        assertFalse(downloadActionIsCurrent(downloads, DownloadId("one"), DownloadAction.SHARE))
        assertFalse(downloadActionIsCurrent(downloads, DownloadId("gone"), DownloadAction.OPEN))
    }

    private fun record(
        id: String,
        actions: Set<DownloadAction> = emptySet(),
    ) = DownloadRecord(
        id = DownloadId(id),
        fileName = "$id.bin",
        host = "files.example",
        totalBytes = 10,
        downloadedBytes = 10,
        state = DownloadState.COMPLETE,
        allowedActions = actions,
    )
}
