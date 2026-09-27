// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DownloadOrganizerIndexTest {
    private val records = listOf(
        download("3", "photo.JPG", "media.example", 50, DownloadState.COMPLETE),
        download("2", "report.pdf", "docs.example", 500, DownloadState.RUNNING),
        download("1", "notes.txt", "docs.example", 10, DownloadState.FAILED),
    )
    private val index = DownloadOrganizerIndex.build(records)

    @Test
    fun newestPreservesTheBrowserChronologyInsteadOfSortingOpaqueIds() {
        val projected = index.project("", DownloadFilter.ALL, DownloadSort.NEWEST, DownloadGrouping.NONE)

        assertEquals(listOf("3", "2", "1"), projected.groups.single().downloads.map { it.id.value })
    }

    @Test
    fun searchIsCaseFoldedAndEveryBoundedTermMustMatchNameOrSite() {
        val projected = index.project(
            "DOCS REPORT",
            DownloadFilter.ALL,
            DownloadSort.NEWEST,
            DownloadGrouping.NONE,
        )

        assertEquals(listOf("report.pdf"), projected.groups.single().downloads.map { it.fileName })
    }

    @Test
    fun filterSortAndGroupAreOneImmutableProjection() {
        val projected = index.project(
            query = "",
            filter = DownloadFilter.ALL,
            sort = DownloadSort.LARGEST,
            grouping = DownloadGrouping.SOURCE,
        )

        assertEquals(listOf("docs.example", "media.example"), projected.groups.map {
            (it.title as DownloadGroupTitle.Source).host
        })
        assertEquals(listOf(500L, 10L), projected.groups.first().downloads.map {
            it.totalBytes
        })
    }

    @Test
    fun fileTypeGroupingUsesOnlyTheBoundedFileNameExtension() {
        val projected = index.project(
            query = "",
            filter = DownloadFilter.ALL,
            sort = DownloadSort.NAME,
            grouping = DownloadGrouping.FILE_TYPE,
        )

        assertEquals(
            listOf(DownloadFileType.DOCUMENT, DownloadFileType.IMAGE),
            projected.groups.map { (it.title as DownloadGroupTitle.FileType).type },
        )
    }

    @Test
    fun queryAndSnapshotBoundsCannotGrowWithUntrustedInput() {
        val query = boundedDownloadQuery("😀".repeat(500))
        val snapshot = DownloadSnapshot.bounded(
            downloads = (0..MAX_ORGANIZER_DOWNLOADS).map { index ->
                download("$index", "$index.bin", "files.example", 1, DownloadState.COMPLETE)
            },
            status = DownloadCollectionStatus.COMPLETE,
        )

        assertEquals(128, query.codePointCount(0, query.length))
        assertEquals(MAX_ORGANIZER_DOWNLOADS, snapshot.downloads.size)
        assertEquals(DownloadCollectionStatus.LIMITED, snapshot.status)
        assertTrue(snapshot.downloads.none { it.id.value == "$MAX_ORGANIZER_DOWNLOADS" })
    }

    private fun download(
        id: String,
        name: String,
        host: String,
        bytes: Long,
        state: DownloadState,
    ) = DownloadRecord(
        id = DownloadId(id),
        fileName = name,
        host = host,
        totalBytes = bytes,
        downloadedBytes = bytes,
        state = state,
        allowedActions = setOf(DownloadAction.REMOVE),
    )
}
