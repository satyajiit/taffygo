// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.ProfileDataErasureCoordinator
import com.taffygo.browser.ui.app.ProfileDataExportEncoder
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.feature.browsing.BookmarksRepository
import com.taffygo.browser.ui.feature.browsing.BookmarksSnapshot
import com.taffygo.browser.ui.feature.browsing.HistoryRepository
import com.taffygo.browser.ui.feature.browsing.HistorySnapshot
import com.taffygo.browser.ui.feature.settings.ProfileDataControl
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import com.taffygo.browser.ui.feature.settings.TimeOnSitesRepository
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.chromium.taffy.shell.ChromiumBrowserMediator
import taffy.core_api.CoreStatus

/** One window's complete export capture and its profile-wide fail-closed erase handoff. */
internal class ChromiumProfileDataControl(
    private val core: ChromiumCoreApiEndpoint,
    private val preferences: UserPreferencesRepository,
    private val history: HistoryRepository,
    private val bookmarks: BookmarksRepository,
    private val browser: ChromiumBrowserMediator,
    private val siteSettings: SiteSettingsRepository,
    private val timeOnSites: TimeOnSitesRepository,
    private val encoder: ProfileDataExportEncoder,
    private val eraser: ProfileDataErasureCoordinator,
    private val dispatchers: AppDispatchers,
    scope: CoroutineScope,
) : ProfileDataControl {
    private data class CoreInputs(
        val core: CoreStatus,
        val preferences: UserPreferences,
        val history: HistorySnapshot,
        val bookmarks: BookmarksSnapshot,
    )

    private data class BrowserInputs(
        val downloads: List<DownloadRecord>,
        val downloadsComplete: Boolean,
        val siteSettings: SiteSettingsRepository.Snapshot,
        val filtering: FilteringSettings,
        val timeOnSites: TimeOnSitesRepository.Snapshot,
    )

    private val operation = Mutex()
    private val erasureStarted = MutableStateFlow(false)
    private val coreInputs = combine(
        core.status,
        preferences.preferences,
        history.snapshot,
        bookmarks.snapshot,
        ::CoreInputs,
    )
    private val browserInputs = combine(
        browser.downloads,
        browser.downloadsComplete,
        siteSettings.snapshot,
        browser.filtering,
        timeOnSites.snapshot,
        ::BrowserInputs,
    )

    override val availability: StateFlow<ProfileDataControl.Availability> = combine(
        coreInputs,
        browserInputs,
        erasureStarted,
    ) { coreValues, browserValues, erasing ->
        ProfileDataControl.Availability(
            exportAvailable = !erasing && encoder.isReady(coreValues.toExportInput(browserValues)),
            deleteAvailable = !erasing,
        )
    }.distinctUntilChanged().stateIn(
        scope,
        SharingStarted.Eagerly,
        currentAvailability(),
    )

    override suspend fun export(
        write: suspend (ByteArray) -> Boolean,
    ): ProfileDataControl.ExportResult = operation.withLock {
        if (erasureStarted.value) return@withLock ProfileDataControl.ExportResult.UNAVAILABLE
        val input = capture()
        val bytes = withContext(dispatchers.default) { encoder.encode(input) }
            ?: return@withLock ProfileDataControl.ExportResult.UNAVAILABLE
        try {
            if (write(bytes)) {
                ProfileDataControl.ExportResult.COMPLETED
            } else {
                ProfileDataControl.ExportResult.FAILED
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: RuntimeException) {
            ProfileDataControl.ExportResult.FAILED
        } finally {
            bytes.fill(0)
        }
    }

    override suspend fun deleteApplicationData(): ProfileDataControl.DeletionResult =
        operation.withLock {
            if (erasureStarted.value) {
                return@withLock ProfileDataControl.DeletionResult.UNAVAILABLE
            }
            erasureStarted.value = true
            val status = core.status.value
            val activeTasks = if (status.hasCompleteProjection()) {
                ProfileDataErasureCoordinator.ActiveTasks.Known(
                    status.active_tasks.map { it.task_id },
                )
            } else {
                ProfileDataErasureCoordinator.ActiveTasks.Unknown
            }
            eraser.erase(activeTasks)
        }

    private fun capture() = ProfileDataExportEncoder.Input(
        core = core.status.value,
        preferences = preferences.preferences.value,
        history = history.snapshot.value,
        bookmarks = bookmarks.snapshot.value,
        downloads = browser.downloads.value,
        downloadsComplete = browser.downloadsComplete.value,
        siteSettings = siteSettings.snapshot.value,
        filtering = browser.filtering.value,
        timeOnSites = timeOnSites.snapshot.value,
    )

    private fun currentAvailability(): ProfileDataControl.Availability {
        val ready = !erasureStarted.value && encoder.isReady(capture())
        return ProfileDataControl.Availability(
            exportAvailable = ready,
            deleteAvailable = !erasureStarted.value,
        )
    }

    private fun CoreInputs.toExportInput(browser: BrowserInputs) =
        ProfileDataExportEncoder.Input(
            core = core,
            preferences = preferences,
            history = history,
            bookmarks = bookmarks,
            downloads = browser.downloads,
            downloadsComplete = browser.downloadsComplete,
            siteSettings = browser.siteSettings,
            filtering = browser.filtering,
            timeOnSites = browser.timeOnSites,
        )
}
