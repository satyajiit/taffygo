// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.feature.browsing.BookmarksSnapshot
import com.taffygo.browser.ui.feature.browsing.HistorySnapshot
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import com.taffygo.browser.ui.feature.settings.TimeOnSitesRepository

/** Browser-owned half of the fixed profile export schema. */
internal fun BoundedJsonWriter.writeBrowserExportSections(
    input: ProfileDataExportEncoder.Input,
) {
    writeHistory(input.history as HistorySnapshot.Ready)
    writeBookmarks(input.bookmarks as BookmarksSnapshot.Ready)
    writeDownloads(input.downloads)
    writeSiteSettings(input.siteSettings as SiteSettingsRepository.Snapshot.Ready)
    writeFiltering(input.filtering)
    writeTimeOnSites(input.timeOnSites)
    writePreferences(input.preferences)
}

private fun BoundedJsonWriter.writeHistory(history: HistorySnapshot.Ready) {
    fieldArray("history", history.visits.sortedWith(
        compareBy({ it.visitedAtEpochMillis }, { it.host }, { it.title }, { it.id.value }),
    )) { visit ->
        objectValue {
            field("title", visit.title)
            field("host", visit.host)
            field("visitedAtEpochMillis", visit.visitedAtEpochMillis)
        }
    }
}

private fun BoundedJsonWriter.writeBookmarks(bookmarks: BookmarksSnapshot.Ready) {
    val rows = bookmarks.folders.flatMap { folder ->
        folder.bookmarks.map { bookmark -> folder.name to bookmark }
    }.sortedWith(compareBy({ it.first }, { it.second.host }, { it.second.title }, { it.second.id.value }))
    fieldArray("bookmarks", rows) { (folder, bookmark) ->
        objectValue {
            field("folder", folder)
            field("title", bookmark.title)
            field("host", bookmark.host)
        }
    }
}

private fun BoundedJsonWriter.writeDownloads(downloads: List<DownloadRecord>) {
    fieldArray("downloads", downloads.sortedWith(
        compareBy({ it.host }, { it.fileName }, { it.id.value }),
    )) { download ->
        objectValue {
            field("fileName", download.fileName)
            field("sourceHost", download.host)
            fieldNumber("totalBytes", download.totalBytes)
            field("downloadedBytes", download.downloadedBytes)
            field("state", download.state.name)
        }
    }
}

private fun BoundedJsonWriter.writeSiteSettings(
    settings: SiteSettingsRepository.Snapshot.Ready,
) {
    fieldObject("siteSettings") {
        fieldArray("defaults", settings.defaults.sortedBy { it.capability.name }) { setting ->
            objectValue {
                field("capability", setting.capability.name)
                field("enabled", setting.enabled)
            }
        }
        fieldArray("sites", settings.sites.sortedBy { it.host }) { site ->
            objectValue {
                field("host", site.host)
                fieldArray("changedCapabilities", site.changedCapabilities.sortedBy { it.name }) {
                    objectValue { field("capability", it.name) }
                }
            }
        }
    }
}

private fun BoundedJsonWriter.writeFiltering(filtering: FilteringSettings) {
    fieldObject("blocking") {
        field("enabled", filtering.enabled)
        field("blockedTotal", filtering.blockedTotal)
        fieldNumber("blockedThisWeek", filtering.blockedThisWeek)
        fieldNumber("minimumSitesThisWeek", filtering.minimumSitesThisWeek)
        fieldArray("exceptionHosts", filtering.exceptionHosts.sorted()) {
            objectValue { field("host", it) }
        }
    }
}

private fun BoundedJsonWriter.writeTimeOnSites(time: TimeOnSitesRepository.Snapshot) {
    fieldObject("timeOnSites") {
        fun writePeriod(name: String, sites: List<TimeOnSitesRepository.Site>) {
            fieldArray(name, sites.sortedWith(compareBy({ it.site }, { it.grouped }))) { site ->
                objectValue {
                    field("site", site.site)
                    field("durationMillis", site.durationMillis)
                    field("grouped", site.grouped)
                }
            }
        }
        writePeriod("today", time.today)
        writePeriod("week", time.week)
    }
}

private fun BoundedJsonWriter.writePreferences(preferences: UserPreferences) {
    fieldObject("preferences") {
        field("theme", preferences.theme.name)
        field("language", preferences.appLanguage.name)
        field("region", preferences.regionCode)
        field("pseudoLocalization", preferences.pseudoLocalization)
        field("forceDarkWeb", preferences.forceDarkWeb)
        field("providerRoute", preferences.providerRoute.name)
        field("onboardingCompleted", preferences.onboardingCompleted)
        field("composerSuggestions", preferences.composerSuggestions)
        fieldArray("notificationTopics", preferences.notificationTopics.sortedBy { it.name }) {
            objectValue { field("topic", it.name) }
        }
    }
}
