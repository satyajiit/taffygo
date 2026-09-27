// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.LibraryAvailability
import taffy.core_api.MAX_LIBRARY_ENTRIES
import taffy.core_api.MAX_MEMORY_RECORDS
import taffy.core_api.MAX_PROVIDER_ROSTER_ENTRIES
import taffy.core_api.MAX_WORKSPACES
import taffy.core_api.MemoryAvailability

/**
 * Content-free retained-data facts plus the two whole-profile actions.
 *
 * A count can cross this boundary; the records it counted cannot. Export and
 * deletion may become available only when one composite owner can verify every
 * browser and core store. A collection of unrelated partial actions is not an
 * implementation of either operation.
 */
interface PrivacyCenterRepository {
    val snapshot: StateFlow<Snapshot>

    suspend fun exportEverything(
        write: suspend (ByteArray) -> Boolean,
    ): ProfileDataControl.ExportResult

    suspend fun deleteEverything(): ProfileDataControl.DeletionResult

    data class Snapshot(
        val savedWorkspaces: PrivacyDataCount = PrivacyDataCount.Loading,
        val libraryItems: PrivacyDataCount = PrivacyDataCount.Loading,
        val memoryItems: PrivacyDataCount = PrivacyDataCount.Loading,
        val connectedProviders: PrivacyDataCount = PrivacyDataCount.Loading,
        val recentDownloads: PrivacyDataCount = PrivacyDataCount.Loading,
        val changedSites: PrivacyDataCount = PrivacyDataCount.Loading,
        val exportAvailable: Boolean = false,
        val deleteAvailable: Boolean = false,
    )
}

/**
 * Window-owned projection over immutable snapshots from the profile's real
 * browser and isolated-core owners.
 *
 * The inputs carry content because their owning screens need it. This adapter
 * emits only six bounded counts and closed availability states, so Privacy
 * Center cannot accidentally retain or render that content.
 */
internal class ProfilePrivacyCenterRepository(
    coreStatus: StateFlow<CoreStatus>,
    downloads: StateFlow<List<DownloadRecord>>,
    siteSettings: StateFlow<SiteSettingsRepository.Snapshot>,
    private val dataControl: ProfileDataControl,
    scope: CoroutineScope,
) : PrivacyCenterRepository {
    private val coreCounts = coreStatus
        .map(::projectCoreCounts)
        .distinctUntilChanged()
    private val downloadCount = downloads
        .map { projectedCount(it.size, MAX_RECENT_DOWNLOADS) }
        .distinctUntilChanged()
    private val changedSiteCount = siteSettings
        .map(::projectChangedSiteCount)
        .distinctUntilChanged()
    private val actionAvailability = dataControl.availability

    override val snapshot: StateFlow<PrivacyCenterRepository.Snapshot> = combine(
        coreCounts,
        downloadCount,
        changedSiteCount,
        actionAvailability,
        ::privacyCenterSnapshot,
    ).stateIn(
        scope = scope,
        started = SharingStarted.Eagerly,
        initialValue = projectPrivacyCenterSnapshot(
            coreStatus.value,
            downloads.value,
            siteSettings.value,
            dataControl.availability.value,
        ),
    )

    override suspend fun exportEverything(write: suspend (ByteArray) -> Boolean) =
        dataControl.export(write)

    override suspend fun deleteEverything() =
        dataControl.deleteApplicationData()
}

/** Deterministic projection with no output field capable of carrying content. */
internal fun projectPrivacyCenterSnapshot(
    status: CoreStatus,
    downloads: List<DownloadRecord>,
    siteSettings: SiteSettingsRepository.Snapshot,
    availability: ProfileDataControl.Availability = ProfileDataControl.Availability(
        exportAvailable = false,
        deleteAvailable = false,
    ),
): PrivacyCenterRepository.Snapshot {
    val coreCounts = projectCoreCounts(status)
    return privacyCenterSnapshot(
        coreCounts = coreCounts,
        recentDownloads = projectedCount(downloads.size, MAX_RECENT_DOWNLOADS),
        changedSites = projectChangedSiteCount(siteSettings),
        availability = availability,
    )
}

private fun privacyCenterSnapshot(
    coreCounts: CoreCounts,
    recentDownloads: PrivacyDataCount,
    changedSites: PrivacyDataCount,
    availability: ProfileDataControl.Availability,
) = PrivacyCenterRepository.Snapshot(
        savedWorkspaces = coreCounts.savedWorkspaces,
        libraryItems = coreCounts.libraryItems,
        memoryItems = coreCounts.memoryItems,
        connectedProviders = coreCounts.connectedProviders,
        recentDownloads = recentDownloads,
        changedSites = changedSites,
        exportAvailable = availability.exportAvailable,
        deleteAvailable = availability.deleteAvailable,
    )

private fun projectChangedSiteCount(
    siteSettings: SiteSettingsRepository.Snapshot,
): PrivacyDataCount = when (siteSettings) {
    SiteSettingsRepository.Snapshot.Loading -> PrivacyDataCount.Loading
    SiteSettingsRepository.Snapshot.Unavailable -> PrivacyDataCount.Unavailable
    is SiteSettingsRepository.Snapshot.Ready ->
        projectedCount(siteSettings.sites.size, MAX_CHANGED_SITES)
}

private fun projectCoreCounts(status: CoreStatus): CoreCounts = when (status.availability) {
    CoreAvailability.STARTING -> CoreCounts(
        savedWorkspaces = PrivacyDataCount.Loading,
        libraryItems = PrivacyDataCount.Loading,
        memoryItems = PrivacyDataCount.Loading,
        connectedProviders = PrivacyDataCount.Loading,
    )
    CoreAvailability.UNAVAILABLE,
    CoreAvailability.CIRCUIT_OPEN,
    -> CoreCounts.unavailable()
    CoreAvailability.READY -> {
        if (!status.hasCompleteProjection()) {
            CoreCounts.unavailable()
        } else {
            CoreCounts(
                savedWorkspaces = status.workspaces.contractCount(MAX_WORKSPACES) { it.saved },
                libraryItems = when (status.library.availability) {
                    LibraryAvailability.AVAILABLE ->
                        status.library.entries.contractCount(MAX_LIBRARY_ENTRIES)
                    LibraryAvailability.PRIVATE_PROFILE,
                    LibraryAvailability.UNAVAILABLE,
                    -> PrivacyDataCount.Unavailable
                },
                memoryItems = when (status.memory.availability) {
                    MemoryAvailability.AVAILABLE ->
                        status.memory.records.contractCount(MAX_MEMORY_RECORDS)
                    MemoryAvailability.PRIVATE_PROFILE,
                    MemoryAvailability.UNAVAILABLE,
                    -> PrivacyDataCount.Unavailable
                },
                connectedProviders = status.provider_roster.contractCount(
                    MAX_PROVIDER_ROSTER_ENTRIES,
                ) { it.stored != null },
            )
        }
    }
}

private fun <T> List<T>.contractCount(maximum: Int): PrivacyDataCount {
    if (size > maximum) return PrivacyDataCount.Unavailable
    return PrivacyDataCount.Known(size)
}

private inline fun <T> List<T>.contractCount(
    maximum: Int,
    include: (T) -> Boolean,
): PrivacyDataCount {
    if (size > maximum) return PrivacyDataCount.Unavailable
    return PrivacyDataCount.Known(count(include))
}

private fun projectedCount(size: Int, maximum: Int): PrivacyDataCount.Known =
    PrivacyDataCount.Known(
        value = size.coerceAtMost(maximum),
        isLowerBound = size > maximum,
    )

private data class CoreCounts(
    val savedWorkspaces: PrivacyDataCount,
    val libraryItems: PrivacyDataCount,
    val memoryItems: PrivacyDataCount,
    val connectedProviders: PrivacyDataCount,
) {
    companion object {
        fun unavailable() = CoreCounts(
            savedWorkspaces = PrivacyDataCount.Unavailable,
            libraryItems = PrivacyDataCount.Unavailable,
            memoryItems = PrivacyDataCount.Unavailable,
            connectedProviders = PrivacyDataCount.Unavailable,
        )
    }
}

// Machine owner: contracts/browsing/schema/contract.json MAX_DOWNLOADS.
private const val MAX_RECENT_DOWNLOADS = 256

// Privacy Center does not retain the browser's unbounded set of changed hosts.
private const val MAX_CHANGED_SITES = 4_096
