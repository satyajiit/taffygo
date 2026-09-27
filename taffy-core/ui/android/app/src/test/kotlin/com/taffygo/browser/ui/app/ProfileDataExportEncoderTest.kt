// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.feature.browsing.BookmarksSnapshot
import com.taffygo.browser.ui.feature.browsing.HistorySnapshot
import com.taffygo.browser.ui.feature.browsing.HistoryVisit
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import com.taffygo.browser.ui.feature.settings.TimeOnSitesRepository
import com.taffygo.browser.ui.feature.settings.YouSurfaceAvailability
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedDetailsView
import taffy.core_api.SavedSignInsView
import taffy.core_api.WorkspaceExportFormat
import taffy.core_api.WorkspaceExportView

class ProfileDataExportEncoderTest {
    private val encoder = ProfileDataExportEncoder()

    @Test
    fun `same immutable profile produces identical sorted bytes and excludes transient authority`() {
        val canary = "SECRET-TRANSIENT-AUTHORITY"
        val first = input(
            history = listOf(
                visit("later", 20),
                visit("earlier", 10),
            ),
            transientCanary = canary,
        )
        val second = input(
            history = listOf(
                visit("earlier", 10),
                visit("later", 20),
            ),
            transientCanary = canary,
        )

        val firstBytes = requireNotNull(encoder.encode(first))
        val secondBytes = requireNotNull(encoder.encode(second))
        assertArrayEquals(firstBytes, secondBytes)
        val text = firstBytes.decodeToString()
        assertFalse(text.contains(canary))
        assertFalse(text.contains("request_id"))
        assertTrue(text.contains("passwords, provider keys, account tokens"))
        assertTrue(text.contains("files already downloaded or exported"))
    }

    @Test
    fun `partial inventory and output beyond the hard bound are refused`() {
        assertNull(
            encoder.encode(
                input().copy(history = HistorySnapshot.Ready(emptyList(), complete = false)),
            ),
        )
        assertNull(
            encoder.encode(
                input(history = listOf(visit("x".repeat(9 * 1024 * 1024), 1))),
            ),
        )
    }

    private fun input(
        history: List<HistoryVisit> = emptyList(),
        transientCanary: String = "ignored",
    ): ProfileDataExportEncoder.Input {
        val base = RecordingProviderCoreApiClient().status.value
        return ProfileDataExportEncoder.Input(
            core = base.copy(
                workspace_export = WorkspaceExportView(
                    request_id = transientCanary,
                    workspace_id = "workspace",
                    revision = 1uL,
                    format = WorkspaceExportFormat.MARKDOWN,
                    content = transientCanary,
                ),
                saved_sign_ins = SavedSignInsView(
                    availability = SavedDataAvailability.READY,
                    revision = 0uL,
                    records = emptyList(),
                ),
                saved_details = SavedDetailsView(
                    availability = SavedDataAvailability.READY,
                    revision = 0uL,
                    people = emptyList(),
                ),
            ),
            preferences = UserPreferences(loaded = true),
            history = HistorySnapshot.Ready(history, complete = true),
            bookmarks = BookmarksSnapshot.Ready(emptyList(), complete = true),
            downloads = emptyList(),
            downloadsComplete = true,
            siteSettings = SiteSettingsRepository.Snapshot.Ready(emptyList(), emptyList()),
            filtering = FilteringSettings(),
            timeOnSites = TimeOnSitesRepository.Snapshot(
                availability = YouSurfaceAvailability.READY,
            ),
        )
    }

    private fun visit(title: String, time: Long) = HistoryVisit(
        id = HistoryVisit.Id("id-$time"),
        title = title,
        host = "$time.example",
        visitedAtEpochMillis = time,
    )
}
