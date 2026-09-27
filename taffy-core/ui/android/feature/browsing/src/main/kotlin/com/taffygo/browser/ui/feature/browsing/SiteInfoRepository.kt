// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.flow.StateFlow

/**
 * Connection, permissions, and desktop-site for the site sheet (SCR-204).
 *
 * Blocking facts already ride `BrowserRepository`. This interface is the rest
 * of "on this site". Its permission implementation hides the profile-wide
 * Chromium catalog: callers can ask only about the selected committed host,
 * and an unavailable answer is distinct from an honest empty one.
 */
interface SiteInfoRepository {

    /** Whether the desktop-site switch can actually change the tab. */
    val desktopSiteAvailable: Boolean

    /** Changes after the permission answer for a host may be different. */
    val permissionRevision: StateFlow<Long>

    /** Whether this tab is asking the site for its desktop layout. */
    fun isDesktopSite(host: String): Boolean

    /** The exact changed permissions for the selected committed regular host. */
    fun permissionsFor(host: String): PermissionState

    /** Re-read permissions only if [host] is still the selected committed regular host. */
    suspend fun refreshPermissions(host: String)

    /** Ask for the desktop layout, or go back to the mobile one. */
    suspend fun setDesktopSite(host: String, enabled: Boolean)

    /** Reset [host]'s permission exceptions and report the verified read-back. */
    suspend fun resetPermissions(host: String): PermissionResetResult

    /** The bounded capability vocabulary the site sheet can name exactly. */
    enum class PermissionCapability {
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

    /** Loading, unavailable, honest empty, or exact changed permission facts. */
    sealed interface PermissionState {
        data object Loading : PermissionState
        data object Unavailable : PermissionState
        data object Empty : PermissionState

        data class Changed(
            val changedCount: Int,
            val capabilities: List<PermissionCapability>,
        ) : PermissionState {
            init {
                require(changedCount > 0) { "A changed permission count must be positive" }
                require(capabilities.distinct().size == capabilities.size) {
                    "Changed permission capabilities must be unique"
                }
                require(capabilities == capabilities.sortedBy(PermissionCapability::ordinal)) {
                    "Changed permission capabilities must use stable enum order"
                }
                require(changedCount >= capabilities.size) {
                    "A changed permission count cannot be smaller than its named capabilities"
                }
            }

            /** Changes Chromium can reset but this bounded vocabulary cannot name. */
            val otherCount: Int
                get() = changedCount - capabilities.size
        }
    }

    enum class PermissionResetResult {
        APPLIED,
        REFUSED,
        FAILED,
        UNAVAILABLE,
    }
}
