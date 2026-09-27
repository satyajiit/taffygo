// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.feature.browsing.SiteInfoRepository
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * Adapts the profile-wide settings snapshot to the selected-site vocabulary.
 *
 * Projection happens once per Chromium snapshot. A sheet read is a bounded
 * map lookup, so recomposition never scans the profile's complete site list.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class ChromiumSiteInfoPermissionAdapter(
    private val settings: SiteSettingsRepository,
    scope: CoroutineScope,
) : ChromiumSiteInfoRepository.PermissionStore {
    private val mutableRevision = MutableStateFlow(0L)
    override val revision: StateFlow<Long> = mutableRevision.asStateFlow()

    @Volatile
    private var projection: Projection = project(settings.snapshot.value)
    private val stateLock = Any()

    @Volatile
    private var closed = false
    private val observer: Job = scope.launch(start = CoroutineStart.UNDISPATCHED) {
        settings.snapshot.collect { snapshot -> publish(snapshot) }
    }

    override fun permissionsFor(host: String): SiteInfoRepository.PermissionState {
        if (closed || host.isBlank()) return SiteInfoRepository.PermissionState.Unavailable
        return projection.forHost(host)
    }

    override fun refresh() {
        if (closed) return
        settings.refresh()
        publish(settings.snapshot.value)
    }

    override suspend fun resetSite(host: String): SiteInfoRepository.PermissionResetResult {
        if (closed) return SiteInfoRepository.PermissionResetResult.UNAVAILABLE
        if (host.isBlank()) return SiteInfoRepository.PermissionResetResult.REFUSED
        val result = settings.resetSite(host)
        // ChromiumSiteSettingsRepository publishes its verified read-back
        // before returning. Pull it synchronously as well as observing it so
        // the result and visible facts cannot race one another.
        publish(settings.snapshot.value)
        return when (result) {
            SiteSettingsRepository.UpdateResult.APPLIED -> {
                if (permissionsFor(host) == SiteInfoRepository.PermissionState.Empty) {
                    SiteInfoRepository.PermissionResetResult.APPLIED
                } else {
                    SiteInfoRepository.PermissionResetResult.FAILED
                }
            }
            SiteSettingsRepository.UpdateResult.UNSUPPORTED,
            SiteSettingsRepository.UpdateResult.REFUSED,
            -> SiteInfoRepository.PermissionResetResult.REFUSED
            SiteSettingsRepository.UpdateResult.FAILED ->
                SiteInfoRepository.PermissionResetResult.FAILED
            SiteSettingsRepository.UpdateResult.UNAVAILABLE ->
                SiteInfoRepository.PermissionResetResult.UNAVAILABLE
        }
    }

    private fun publish(snapshot: SiteSettingsRepository.Snapshot) {
        if (closed) return
        val next = project(snapshot)
        synchronized(stateLock) {
            if (closed) return
            if (projection == next) return
            projection = next
            advanceRevision()
        }
    }

    override fun close() {
        synchronized(stateLock) {
            if (closed) return
            closed = true
            projection = Projection.Unavailable
            advanceRevision()
        }
        observer.cancel()
    }

    private fun advanceRevision() {
        mutableRevision.update { revision ->
            if (revision == Long.MAX_VALUE) 0L else revision + 1L
        }
    }

    private sealed interface Projection {
        fun forHost(host: String): SiteInfoRepository.PermissionState

        data object Loading : Projection {
            override fun forHost(host: String) = SiteInfoRepository.PermissionState.Loading
        }

        data object Unavailable : Projection {
            override fun forHost(host: String) = SiteInfoRepository.PermissionState.Unavailable
        }

        data class Ready(
            private val sites: Map<String, SiteInfoRepository.PermissionState>,
        ) : Projection {
            override fun forHost(host: String): SiteInfoRepository.PermissionState =
                sites[host] ?: SiteInfoRepository.PermissionState.Empty
        }
    }

    private companion object {
        fun project(snapshot: SiteSettingsRepository.Snapshot): Projection = when (snapshot) {
            SiteSettingsRepository.Snapshot.Loading -> Projection.Loading
            SiteSettingsRepository.Snapshot.Unavailable -> Projection.Unavailable
            is SiteSettingsRepository.Snapshot.Ready -> {
                val sites = LinkedHashMap<String, SiteInfoRepository.PermissionState>(
                    snapshot.sites.size,
                )
                snapshot.sites.forEach { entry ->
                    if (entry.host.isBlank() || entry.host == "*") return@forEach
                    val capabilities = entry.changedCapabilities
                        .map(::mapCapability)
                        .sortedBy(SiteInfoRepository.PermissionCapability::ordinal)
                    val state = if (
                        entry.changedPermissionCount <= 0 ||
                        entry.changedPermissionCount < capabilities.size
                    ) {
                        SiteInfoRepository.PermissionState.Unavailable
                    } else {
                        SiteInfoRepository.PermissionState.Changed(
                            changedCount = entry.changedPermissionCount,
                            capabilities = capabilities,
                        )
                    }
                    sites[entry.host] = if (sites.containsKey(entry.host)) {
                        // Duplicate host facts violate the settings contract;
                        // do not let list order decide what the sheet claims.
                        SiteInfoRepository.PermissionState.Unavailable
                    } else {
                        state
                    }
                }
                Projection.Ready(sites)
            }
        }

        fun mapCapability(
            capability: SiteSettingsRepository.Capability,
        ): SiteInfoRepository.PermissionCapability = when (capability) {
            SiteSettingsRepository.Capability.LOCATION ->
                SiteInfoRepository.PermissionCapability.LOCATION
            SiteSettingsRepository.Capability.CAMERA ->
                SiteInfoRepository.PermissionCapability.CAMERA
            SiteSettingsRepository.Capability.MICROPHONE ->
                SiteInfoRepository.PermissionCapability.MICROPHONE
            SiteSettingsRepository.Capability.NOTIFICATIONS ->
                SiteInfoRepository.PermissionCapability.NOTIFICATIONS
            SiteSettingsRepository.Capability.JAVASCRIPT ->
                SiteInfoRepository.PermissionCapability.JAVASCRIPT
            SiteSettingsRepository.Capability.POP_UPS ->
                SiteInfoRepository.PermissionCapability.POP_UPS
            SiteSettingsRepository.Capability.AUTOMATIC_DOWNLOADS ->
                SiteInfoRepository.PermissionCapability.AUTOMATIC_DOWNLOADS
            SiteSettingsRepository.Capability.CLIPBOARD ->
                SiteInfoRepository.PermissionCapability.CLIPBOARD
            SiteSettingsRepository.Capability.SENSORS ->
                SiteInfoRepository.PermissionCapability.SENSORS
            SiteSettingsRepository.Capability.SOUND ->
                SiteInfoRepository.PermissionCapability.SOUND
        }
    }
}
