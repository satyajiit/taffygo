// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * What the site sheet is told, state by state.
 *
 * This projection decides nothing about blocking. It carries the browser's own
 * verdicts — is blocking acting here, is this site allowed — and chooses which
 * of them the sheet shows. That is the point: the toggle a person sees agrees
 * with the request path because it is the request path's answer, asked of the
 * plane this tab belongs to (decision 0128).
 */
class SiteFilteringReducerTest {

    @Test
    fun `the page's own facts are carried through`() {
        val state = projectSiteFiltering(
            NavigationState(
                host = "news.example.test",
                title = "News",
                filteringActive = true,
                blockedRequestCount = 12,
            ),
            FilteringSettings(),
        )
        assertEquals("news.example.test", state.host)
        assertTrue(state.filteringActive)
        assertEquals(12, state.blockedCount)
        assertTrue(state.canToggleSite)
        assertFalse(state.excepted)
    }

    /**
     * The subdomain rule is not tested here any more, because it is not
     * decided here any more.
     *
     * This projection used to carry its own copy of the browser's rule and
     * match it against the profile's exception list. That copy could not see
     * which profile applied, which is the defect decision 0128 was written for,
     * and a second copy of a rule is a second answer that can drift. The rule
     * now lives only in `taffy-core/components/filtering/core/posture.cc`,
     * where `posture_unittest.cc` pins the label boundary — so its coverage is
     * `taffy_unittests`, and not this lane.
     *
     * What is left to check here is that the browser's answer is carried
     * through untouched, including when it disagrees with the list the screen
     * happens to be holding.
     */
    @Test
    fun `the browser's own verdict is carried through, not recomputed`() {
        fun exceptedOn(siteExcepted: Boolean, hosts: List<String>) = projectSiteFiltering(
            NavigationState(
                host = "news.example.test",
                title = "",
                siteExcepted = siteExcepted,
            ),
            FilteringSettings(exceptionHosts = hosts),
        ).excepted

        assertTrue(exceptedOn(siteExcepted = true, hosts = emptyList()))
        assertFalse(exceptedOn(siteExcepted = false, hosts = listOf("example.test")))
    }

    @Test
    fun `the master toggle off takes the site toggle with it`() {
        val state = projectSiteFiltering(
            NavigationState(host = "news.example.test", title = ""),
            FilteringSettings(enabled = false),
        )
        assertFalse(state.enabled)
        assertFalse(state.canToggleSite)
    }

    @Test
    fun `connection and desktop facts are carried through`() {
        val permissions = SiteInfoRepository.PermissionState.Changed(
            changedCount = 2,
            capabilities = listOf(
                SiteInfoRepository.PermissionCapability.CAMERA,
                SiteInfoRepository.PermissionCapability.MICROPHONE,
            ),
        )
        val state = projectSiteFiltering(
            NavigationState(host = "news.example.test", title = "News", isSecure = true),
            FilteringSettings(),
            desktopSite = true,
            desktopSiteAvailable = false,
            permissions = permissions,
            permissionReset = SiteFilteringUiState.ActionProgress.FAILED,
        )

        assertTrue(state.isSecure)
        assertTrue(state.desktopSite)
        assertFalse(state.desktopSiteAvailable)
        assertEquals(permissions, state.permissions)
        assertEquals(SiteFilteringUiState.ActionProgress.FAILED, state.permissionReset)
        assertTrue(state.canResetPermissions)
    }

    @Test
    fun `permission loading unavailable and empty stay distinct`() {
        val states = listOf(
            SiteInfoRepository.PermissionState.Loading,
            SiteInfoRepository.PermissionState.Unavailable,
            SiteInfoRepository.PermissionState.Empty,
        )

        assertEquals(
            states,
            states.map { permissions ->
                projectSiteFiltering(
                    NavigationState(host = "news.example.test", title = "News"),
                    FilteringSettings(),
                    permissions = permissions,
                ).permissions
            },
        )
    }

    @Test
    fun `page zoom is carried without recomputing chromium truth`() {
        val zoom = PageZoomState(
            available = true,
            percent = 125,
            canZoomOut = true,
            canZoomIn = true,
            canReset = true,
        )
        val state = projectSiteFiltering(
            NavigationState(host = "news.example.test", title = "News"),
            FilteringSettings(),
            pageZoom = zoom,
        )

        assertEquals(zoom, state.pageZoom)
    }

    @Test
    fun `a tab that has been nowhere offers no site toggle`() {
        val state = projectSiteFiltering(
            NavigationState(host = "", title = ""),
            FilteringSettings(),
            permissions = SiteInfoRepository.PermissionState.Changed(
                changedCount = 1,
                capabilities = listOf(SiteInfoRepository.PermissionCapability.CAMERA),
            ),
        )
        assertFalse(state.canToggleSite)
        assertFalse(state.canResetPermissions)
        assertFalse(state.excepted)
    }
}
