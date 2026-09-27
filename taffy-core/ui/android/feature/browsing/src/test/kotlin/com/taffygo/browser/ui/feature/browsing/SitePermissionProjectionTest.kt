// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SitePermissionProjectionTest {
    @Test
    fun `site permissions reach only the exact selected regular host`() {
        val permissions = SiteInfoRepository.PermissionState.Changed(
            changedCount = 1,
            capabilities = listOf(SiteInfoRepository.PermissionCapability.CAMERA),
        )
        val matching = projectBrowserMain(
            NavigationState(HOST, "Retention policy"),
            tabs(),
            permissions = permissions,
            permissionResetConfirmation = true,
        )
        val private = projectBrowserMain(
            NavigationState(HOST, "Retention policy"),
            tabs(private = true),
            permissions = permissions,
            permissionResetConfirmation = true,
        )
        val mismatched = projectBrowserMain(
            NavigationState("other.example.test", "Other"),
            tabs(),
            permissions = permissions,
            permissionResetConfirmation = true,
        )

        assertEquals(permissions, matching.siteFiltering.permissions)
        assertTrue(matching.siteFiltering.permissionResetConfirmation)
        assertEquals(
            SiteInfoRepository.PermissionState.Unavailable,
            private.siteFiltering.permissions,
        )
        assertFalse(private.siteFiltering.permissionResetConfirmation)
        assertEquals(
            SiteInfoRepository.PermissionState.Unavailable,
            mismatched.siteFiltering.permissions,
        )
        assertEquals(
            SiteFilteringUiState.ActionProgress.IDLE,
            mismatched.siteFiltering.permissionReset,
        )
        assertFalse(mismatched.siteFiltering.permissionResetConfirmation)
    }

    @Test
    fun `confirmation disappears when permission facts stop being changed`() {
        val state = projectBrowserMain(
            NavigationState(HOST, "Retention policy"),
            tabs(),
            permissions = SiteInfoRepository.PermissionState.Loading,
            permissionResetConfirmation = true,
        )

        assertEquals(SiteInfoRepository.PermissionState.Loading, state.siteFiltering.permissions)
        assertFalse(state.siteFiltering.permissionResetConfirmation)
    }

    private fun tabs(private: Boolean = false) = listOf(
        Tab(
            id = TabId("selected"),
            title = "Retention policy",
            host = HOST,
            isPrivate = private,
            isSelected = true,
        ),
    )

    private companion object {
        const val HOST = "docs.example.test"
    }
}
