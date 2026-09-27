// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SiteSettingsReducerTest {

    @Test
    fun `empty ready is an empty list, not unavailable`() {
        val state = projectSiteSettings(EmptySiteSettingsRepository().snapshot.value)
        assertTrue(state.available)
        assertFalse(state.loading)
        assertTrue(state.sites.isEmpty())
    }

    @Test
    fun `unavailable is a different sentence from empty`() {
        val state = projectSiteSettings(UnavailableSiteSettingsRepository().snapshot.value)
        assertFalse(state.available)
        assertTrue(state.sites.isEmpty())
    }

    @Test
    fun `ready carries hosts without inventing permissions`() {
        val state = projectSiteSettings(
            SiteSettingsRepository.Snapshot.Ready(
                defaults = listOf(
                    SiteSettingsRepository.DefaultSetting(
                        SiteSettingsRepository.Capability.CAMERA,
                        enabled = true,
                        userModifiable = true,
                    ),
                ),
                sites = listOf(SiteSettingsRepository.Entry("news.example.test", 2)),
            ),
        )
        assertTrue(state.defaults.single().enabled)
        assertEquals("news.example.test", state.sites.single().host)
        assertEquals(2, state.sites.single().changedPermissionCount)
    }

    @Test
    fun `only a supported modifiable changed default starts a write`() {
        val state = SiteSettingsUiState(
            defaults = listOf(
                SiteSettingsUiState.Default(
                    SiteSettingsRepository.Capability.CAMERA,
                    enabled = true,
                    userModifiable = true,
                ),
                SiteSettingsUiState.Default(
                    SiteSettingsRepository.Capability.MICROPHONE,
                    enabled = true,
                    userModifiable = false,
                ),
            ),
        )
        val changed = reduceSiteSettings(
            state,
            SiteSettingsIntent.SetDefault(SiteSettingsRepository.Capability.CAMERA, false),
        )
        assertEquals(SiteSettingsRepository.Capability.CAMERA, changed.updating)
        assertEquals(
            state,
            reduceSiteSettings(
                state,
                SiteSettingsIntent.SetDefault(SiteSettingsRepository.Capability.MICROPHONE, false),
            ),
        )
        assertEquals(
            state,
            reduceSiteSettings(
                state,
                SiteSettingsIntent.SetDefault(SiteSettingsRepository.Capability.SENSORS, false),
            ),
        )
    }

    @Test
    fun `retry leaves the snapshot to the repository`() {
        val state = SiteSettingsUiState(available = false)
        assertEquals(state, reduceSiteSettings(state, SiteSettingsIntent.Retry))
    }

    @Test
    fun `reset confirmation can name only a currently published site`() {
        val state = SiteSettingsUiState(
            sites = listOf(SiteSettingsUiState.Site("camera.example.test", 1)),
        )

        assertEquals(
            state,
            reduceSiteSettings(
                state,
                SiteSettingsIntent.RequestSiteReset("forged.example.test"),
            ),
        )
        val confirming = reduceSiteSettings(
            state,
            SiteSettingsIntent.RequestSiteReset("camera.example.test"),
        )
        assertEquals("camera.example.test", confirming.resetCandidate)
        val resetting = reduceSiteSettings(confirming, SiteSettingsIntent.ConfirmSiteReset)
        assertEquals(null, resetting.resetCandidate)
        assertEquals("camera.example.test", resetting.resettingHost)
    }

    @Test
    fun `dismiss closes a reset confirmation without changing its site row`() {
        val state = SiteSettingsUiState(
            sites = listOf(SiteSettingsUiState.Site("camera.example.test", 1)),
            resetCandidate = "camera.example.test",
        )

        assertEquals(
            state.copy(resetCandidate = null),
            reduceSiteSettings(state, SiteSettingsIntent.Dismiss),
        )
    }
}
