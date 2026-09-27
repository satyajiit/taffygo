// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Live global defaults and sites whose permissions this phone has changed.
 *
 * Ready with an empty list is honest. Unavailable is a different sentence.
 * Neither mints a permission grant.
 */
interface SiteSettingsRepository {

    val snapshot: StateFlow<Snapshot>

    /** Re-read the current original-profile permission exceptions. */
    fun refresh()

    /** Write one supported default and report only an exact read-back as applied. */
    suspend fun setDefault(capability: Capability, enabled: Boolean): UpdateResult

    /** Reset every permission exception for one exact host and verify the live read-back. */
    suspend fun resetSite(host: String): UpdateResult

    sealed interface Snapshot {
        data object Loading : Snapshot
        data object Unavailable : Snapshot
        data class Ready(
            val defaults: List<DefaultSetting>,
            val sites: List<Entry>,
        ) : Snapshot {
            /** A site exception wins over the global default for that capability. */
            fun sourceFor(host: String, capability: Capability): SettingSource? {
                val site = sites.firstOrNull { it.host == host }
                if (site != null && capability in site.changedCapabilities) {
                    return SettingSource.SITE_EXCEPTION
                }
                return if (defaults.any { it.capability == capability }) {
                    SettingSource.GLOBAL_DEFAULT
                } else {
                    null
                }
            }
        }
    }

    /** The deliberately bounded SCR-205 capability set. */
    enum class Capability {
        LOCATION,
        CAMERA,
        MICROPHONE,
        NOTIFICATIONS,
        JAVASCRIPT,
        POP_UPS,
        AUTOMATIC_DOWNLOADS,
        CLIPBOARD,
        SENSORS,
        SOUND,
    }

    data class DefaultSetting(
        val capability: Capability,
        val enabled: Boolean,
        val userModifiable: Boolean,
    )

    data class Entry(
        val host: String,
        val changedPermissionCount: Int,
        val changedCapabilities: Set<Capability> = emptySet(),
    )

    enum class SettingSource { GLOBAL_DEFAULT, SITE_EXCEPTION }

    enum class UpdateResult { APPLIED, UNSUPPORTED, REFUSED, FAILED, UNAVAILABLE }
}

internal class EmptySiteSettingsRepository : SiteSettingsRepository {
    override val snapshot: StateFlow<SiteSettingsRepository.Snapshot> =
        MutableStateFlow(
            SiteSettingsRepository.Snapshot.Ready(defaults = emptyList(), sites = emptyList()),
        ).asStateFlow()

    override fun refresh() = Unit

    override suspend fun setDefault(
        capability: SiteSettingsRepository.Capability,
        enabled: Boolean,
    ) = SiteSettingsRepository.UpdateResult.UNSUPPORTED

    override suspend fun resetSite(host: String) = SiteSettingsRepository.UpdateResult.UNSUPPORTED
}

internal class UnavailableSiteSettingsRepository : SiteSettingsRepository {
    override val snapshot: StateFlow<SiteSettingsRepository.Snapshot> =
        MutableStateFlow(SiteSettingsRepository.Snapshot.Unavailable).asStateFlow()

    override fun refresh() = Unit

    override suspend fun setDefault(
        capability: SiteSettingsRepository.Capability,
        enabled: Boolean,
    ) = SiteSettingsRepository.UpdateResult.UNAVAILABLE

    override suspend fun resetSite(host: String) = SiteSettingsRepository.UpdateResult.UNAVAILABLE
}
