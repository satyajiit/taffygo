// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * Screen SCR-204, the site sheet for ad and tracker blocking.
 *
 * A sheet over screen SCR-101 rather than a destination: it is about the page
 * in front of the person, and it must close over the page it describes the
 * way the top bar's menu does. Everything here is projected from browser
 * truth by `projectSiteFiltering`; the only chrome state — whether the sheet
 * is open — lives on [BrowserMainUiState] beside the menu's.
 */
data class SiteFilteringUiState(
    /** The host the sheet is about: the selected tab's committed page. */
    val host: String = "",
    /** Whether blocking is acting on this page right now. */
    val filteringActive: Boolean = false,
    /** Requests blocked on this page so far. May jump, never lies. */
    val blockedCount: Int = 0,
    /** The profile's master toggle. Off means no site toggle can act. */
    val enabled: Boolean = true,
    /**
     * Whether an exception covers this host — the host itself, or a parent
     * whose exception covers every subdomain.
     *
     * Answered by the browser, on the plane this tab belongs to, so a private
     * tab reports its own allowances rather than the regular profile's
     * (decision 0128). The rule itself is not restated up here.
     */
    val excepted: Boolean = false,
    /** Whether the page is being served over a private connection. */
    val isSecure: Boolean = false,
    /** Whether this tab is asking the site for its desktop layout. */
    val desktopSite: Boolean = false,
    /** Whether the desktop-site switch can actually change the tab. */
    val desktopSiteAvailable: Boolean = false,
    /** Changed permissions for the exact selected committed regular host. */
    val permissions: SiteInfoRepository.PermissionState =
        SiteInfoRepository.PermissionState.Unavailable,
    /** Progress/result for the explicit permission reset action on this host. */
    val permissionReset: ActionProgress = ActionProgress.IDLE,
    /** Whether the host-named reset confirmation is covering this sheet. */
    val permissionResetConfirmation: Boolean = false,
    /**
     * Progress/result for turning blocking back on for this site.
     *
     * The seam answers whether it recorded the change, and both callers used
     * to throw that answer away. A refusal is now shown rather than leaving a
     * switch that appears to have moved and has not.
     */
    val siteBlocking: ActionProgress = ActionProgress.IDLE,
    /** The selected page's persistent layout zoom. */
    val pageZoom: PageZoomState = PageZoomState(),
) {
    /**
     * Whether the sheet may offer the site toggle at all: there has to be a
     * host to record, and the master toggle has to be on for a site toggle
     * to mean anything.
     */
    val canToggleSite: Boolean
        get() = host.isNotBlank() && enabled

    val canResetPermissions: Boolean
        get() = host.isNotBlank() &&
            permissions is SiteInfoRepository.PermissionState.Changed &&
            permissionReset != ActionProgress.RUNNING &&
            !permissionResetConfirmation

    /**
     * How an explicit action on this sheet is going.
     *
     * One type for both actions the sheet runs — resetting this site's
     * permissions, and turning blocking back on for it — because they have the
     * same four states and a name mentioning only one of them would be a lie.
     * `SUCCEEDED` is silent on the surface: the switch has moved, or the row
     * has left the list, and saying so again is noise.
     */
    enum class ActionProgress {
        IDLE,
        RUNNING,
        SUCCEEDED,
        FAILED,
    }
}
