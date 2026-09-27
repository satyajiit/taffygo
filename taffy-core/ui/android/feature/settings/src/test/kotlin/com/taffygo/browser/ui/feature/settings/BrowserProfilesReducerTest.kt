// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BrowserProfilesReducerTest {
    private val current = BrowserProfilesRepository.Profile("current", "Personal", active = true)
    private val work = BrowserProfilesRepository.Profile("work", "Work", active = false)

    @Test
    fun `ready requires exactly one active profile`() {
        val valid = projectBrowserProfiles(
            BrowserProfilesRepository.Snapshot(
                BrowserProfilesRepository.Availability.READY,
                listOf(current, work),
            ),
            BrowserProfilesUiState(),
        )
        assertEquals(BrowserProfilesRepository.Availability.READY, valid.availability)
        assertEquals(listOf(current, work), valid.profiles)

        val noActive = projectBrowserProfiles(
            BrowserProfilesRepository.Snapshot(
                BrowserProfilesRepository.Availability.READY,
                listOf(current.copy(active = false), work),
            ),
            BrowserProfilesUiState(),
        )
        assertEquals(BrowserProfilesRepository.Availability.UNAVAILABLE, noActive.availability)
    }

    @Test
    fun `duplicate opaque ids fail closed before reaching the UI`() {
        val projected = projectBrowserProfiles(
            BrowserProfilesRepository.Snapshot(
                BrowserProfilesRepository.Availability.READY,
                listOf(current, work.copy(id = current.id)),
            ),
            BrowserProfilesUiState(),
        )
        assertEquals(BrowserProfilesRepository.Availability.UNAVAILABLE, projected.availability)
        assertTrue(projected.profiles.isEmpty())
    }

    @Test
    fun `the active profile is never a delete candidate`() {
        val state = BrowserProfilesUiState(
            availability = BrowserProfilesRepository.Availability.READY,
            profiles = listOf(current, work),
        )
        assertNull(
            reduceBrowserProfiles(
                state,
                BrowserProfilesIntent.AskToDelete(current.id),
            ).deleteCandidate,
        )
        assertEquals(
            work,
            reduceBrowserProfiles(
                state,
                BrowserProfilesIntent.AskToDelete(work.id),
            ).deleteCandidate,
        )
    }

    @Test
    fun `a delete in progress holds its sheet open`() {
        val busy = BrowserProfilesUiState(
            availability = BrowserProfilesRepository.Availability.READY,
            profiles = listOf(current, work),
            deleteCandidate = work,
            operation = BrowserProfilesUiState.Operation.DELETING,
        )
        assertTrue(busy.busy)
        assertEquals(busy, reduceBrowserProfiles(busy, BrowserProfilesIntent.DismissDelete))
        assertEquals(busy, reduceBrowserProfiles(busy, BrowserProfilesIntent.AskToDelete(work.id)))
    }

    @Test
    fun `profile in use has its own actionable failure copy`() {
        assertEquals(
            R.string.taffy_profiles_error_in_use,
            BrowserProfilesRepository.Failure.PROFILE_IN_USE.messageResource(),
        )
    }

    @Test
    fun `the current profile's workspace count rides through the projection`() {
        val projected = projectBrowserProfiles(
            BrowserProfilesRepository.Snapshot(
                BrowserProfilesRepository.Availability.READY,
                listOf(current, work),
            ),
            BrowserProfilesUiState(),
            activeWorkspaceCount = 3,
        )
        assertEquals(3, projected.activeWorkspaceCount)

        val unknown = projectBrowserProfiles(
            BrowserProfilesRepository.Snapshot(),
            BrowserProfilesUiState(),
        )
        assertNull(unknown.activeWorkspaceCount)
    }
}
