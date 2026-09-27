// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-401's reducer, identity projection, and search.
 *
 * The search takes localized display text as a function so the rule stays
 * testable without a resource table.
 */
class SettingsHomeReducerTest {

    private val searchTextOf: (SettingsSection) -> List<String> = { section ->
        when (section) {
            SettingsSection.AD_AND_TRACKER_BLOCKING -> listOf(
                "Ads and trackers",
                "On for every site, minus the ones you allow",
                "ads trackers blocking",
            )
            SettingsSection.TAFFY -> listOf("Taffy", "Skills, and how Taffy talks", "Taffy Personality skills")
            SettingsSection.PRIVACY -> listOf("Privacy", "What's kept on this phone")
            SettingsSection.APPEARANCE -> listOf("Appearance", "Theme, language and text size")
            SettingsSection.NOTIFICATIONS -> listOf("Notifications", "Task progress and downloads")
            SettingsSection.GENERAL -> listOf("General", "Search engine and files")
            SettingsSection.PROFILES -> listOf(
                "Profiles",
                "Keep work, personal browsing, and Taffy data apart",
            )
            SettingsSection.ABOUT_AND_HELP -> listOf("About and help", "Version and how to reach us")
            SettingsSection.BACKUP -> listOf("Backup", "Choose content for an encrypted file, or check an existing backup")
            SettingsSection.YOU -> listOf(
                "You",
                "Your account and what's saved on this phone",
                "You account signed in password",
            )
            SettingsSection.TIME_ON_SITES -> listOf(
                "Time on sites",
                "How long this browser spent on each site this week",
                "time on sites time this week",
            )
            SettingsSection.MEMORY -> listOf(
                "Memory",
                "What Taffy remembers about you",
                "Memory remember note",
            )
            SettingsSection.SAVED_SIGN_INS -> listOf(
                "Saved sign-ins",
                "Passwords and passkeys this browser can fill",
                "password passwords passkey",
            )
            SettingsSection.SAVED_DETAILS -> listOf(
                "Saved details",
                "Name, address, email, and phone kept for forms",
                "saved details name address email",
            )
            SettingsSection.WHAT_HAPPENED -> listOf(
                "What happened",
                "Taffy's actions and your approvals, no page content",
                "What happened Taffy did",
            )
            SettingsSection.LIBRARY -> listOf(
                "Library",
                "Things you chose to keep",
                "Library keep this",
            )
            SettingsSection.PERSONALITY -> listOf(
                "How Taffy talks",
                "Personality — how Taffy writes, and how careful it is",
                "Personality how Taffy talks",
            )
            SettingsSection.WHAT_TAFFY_CAN_DO -> listOf(
                "What Taffy can do",
                "Skills on this phone",
                "skill skills abilities",
            )
            SettingsSection.AI_AND_PROVIDERS -> listOf(
                "AI and providers",
                "Model access and provider keys",
                "AI providers your keys key",
            )
        }
    }

    @Test
    fun `typing changes the query and nothing else`() {
        val before = SettingsHomeUiState(
            displayName = "Ada",
            blockedThisWeek = 4L,
        )
        val after = reduceSettingsHome(before, SettingsHomeIntent.QueryChanged("appear"))
        assertEquals("appear", after.query)
        assertTrue(after.searchOpen)
        assertEquals(before.sections, after.sections)
        assertEquals("Ada", after.displayName)
        assertEquals(4L, after.blockedThisWeek)
    }

    @Test
    fun `toggle search opens and closing it clears the query`() {
        val opened = reduceSettingsHome(SettingsHomeUiState(), SettingsHomeIntent.ToggleSearch)
        assertTrue(opened.searchOpen)
        val closed = reduceSettingsHome(
            opened.copy(query = "appear"),
            SettingsHomeIntent.ToggleSearch,
        )
        assertFalse(closed.searchOpen)
        assertEquals("", closed.query)
    }

    @Test
    fun `clearing the typed query keeps the field open`() {
        val after = reduceSettingsHome(
            SettingsHomeUiState(searchOpen = true, query = "appear"),
            SettingsHomeIntent.QueryChanged(""),
        )
        assertTrue(after.searchOpen)
        assertEquals("", after.query)
    }

    @Test
    fun `toggle search closes a leftover query even if the field flag is off`() {
        val after = reduceSettingsHome(
            SettingsHomeUiState(searchOpen = false, query = "appear"),
            SettingsHomeIntent.ToggleSearch,
        )
        assertFalse(after.searchOpen)
        assertEquals("", after.query)
    }

    @Test
    fun `a stored query with no open flag still restores an open field`() {
        val restored = restoreSettingsHome(query = "appear", searchOpen = null)
        assertEquals("appear", restored.query)
        assertTrue(restored.searchOpen)
        assertTrue(restored.searching)
    }

    @Test
    fun `an explicit closed flag stays closed when the query is empty`() {
        val restored = restoreSettingsHome(query = "", searchOpen = false)
        assertFalse(restored.searchOpen)
        assertFalse(restored.searching)
    }

    @Test
    fun `spaces alone are not a search`() {
        assertFalse(SettingsHomeUiState(query = "   ").searching)
        assertTrue(SettingsHomeUiState(query = "appear").searching)
    }

    @Test
    fun `opening a destination leaves the state to the navigator`() {
        val state = SettingsHomeUiState(query = "appear", displayName = "Ada")
        assertEquals(
            state,
            reduceSettingsHome(state, SettingsHomeIntent.Open(TaffyDestination.Appearance)),
        )
        assertEquals(
            state,
            reduceSettingsHome(state, SettingsHomeIntent.Open(TaffyDestination.You)),
        )
    }

    /**
     * The profile is the identity, and a stale name already on the state is
     * not. The doorway keeps what the person typed into the search field and
     * replaces everything it is told by a port.
     */
    @Test
    fun `the profile replaces a stale name and keeps the query`() {
        val projected = projectSettingsHome(
            local = SettingsHomeUiState(query = "x", displayName = "stale"),
            week = BlockingWeekRepository.Snapshot(),
            profile = LocalProfile(),
        )
        assertEquals("x", projected.query)
        assertNull(projected.displayName)
        assertNull(projected.blockedThisWeek)

        val named = projectSettingsHome(
            local = SettingsHomeUiState(query = "you"),
            week = BlockingWeekRepository.Snapshot(blockedThisWeek = 4L),
            profile = LocalProfile(displayName = "Ada"),
        )
        assertEquals("you", named.query)
        assertEquals("Ada", named.displayName)
        assertEquals(4L, named.blockedThisWeek)
    }

    /**
     * The doorway's face comes from the profile and from nowhere else. The
     * failure that made this a separate test is that a projection which
     * forgets the profile argument still compiles and still draws a face —
     * the fallback monogram, for ever, on a phone whose owner chose a
     * picture.
     */
    @Test
    fun `the doorway face comes from the profile`() {
        val chosen = projectSettingsHome(
            local = SettingsHomeUiState(),
            week = BlockingWeekRepository.Snapshot(),
            profile = LocalProfile(displayName = "Ada Lovelace", avatar = LocalAvatar.of("b3")),
        )
        assertEquals(LocalAvatar.of("b3"), chosen.avatar)
        assertEquals("AL", chosen.monogram)
        assertEquals("Ada Lovelace", chosen.displayName)

        val empty = projectSettingsHome(
            local = SettingsHomeUiState(),
            week = BlockingWeekRepository.Snapshot(),
            profile = LocalProfile(),
        )
        assertEquals(LocalAvatar.Monogram, empty.avatar)
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, empty.monogram)
        assertNull(empty.displayName)
    }

    @Test
    fun `a zero week is a real zero, not an absence`() {
        val projected = projectSettingsHome(
            local = SettingsHomeUiState(),
            week = BlockingWeekRepository.Snapshot(blockedThisWeek = 0L),
            profile = LocalProfile(),
        )
        assertEquals(0L, projected.blockedThisWeek)
    }

    @Test
    fun `settings home is never a grid`() {
        assertFalse(SettingsHomeUiState().showsCategoryGrid(compact = true))
        assertFalse(SettingsHomeUiState(query = "   ").showsCategoryGrid(compact = true))
        assertFalse(SettingsHomeUiState().showsCategoryGrid(compact = false))
        assertFalse(SettingsHomeUiState(query = "Taffy").showsCategoryGrid(compact = true))
    }

    @Test
    fun `a blank query matches only home rows`() {
        assertEquals(
            SettingsSection.entries.filter { it.listedOnHome },
            SettingsHomeUiState(query = "   ").matching(searchTextOf),
        )
        assertEquals(9, SettingsSection.entries.count { it.listedOnHome })
    }

    @Test
    fun `the search ignores case and matches the middle of a name`() {
        assertEquals(
            listOf(SettingsSection.AI_AND_PROVIDERS),
            SettingsHomeUiState(query = "PROVID").matching(searchTextOf),
        )
    }

    @Test
    fun `ads and trackers is a home section search hit`() {
        assertEquals(
            listOf(SettingsSection.AD_AND_TRACKER_BLOCKING),
            SettingsHomeUiState(query = "Ads and trackers").matching(searchTextOf),
        )
        assertTrue(
            SettingsSection.AD_AND_TRACKER_BLOCKING in
                SettingsHomeUiState(query = "ads").matching(searchTextOf),
        )
    }

    @Test
    fun `the search also matches a section summary`() {
        assertEquals(
            listOf(SettingsSection.NOTIFICATIONS),
            SettingsHomeUiState(query = "download").matching(searchTextOf),
        )
        assertEquals(
            listOf(SettingsSection.APPEARANCE),
            SettingsHomeUiState(query = "text size").matching(searchTextOf),
        )
    }

    @Test
    fun `search finds you memory saved sign-ins skills taffy and library`() {
        val password = SettingsHomeUiState(query = "password").matching(searchTextOf)
        assertTrue(SettingsSection.YOU in password)
        assertTrue(SettingsSection.SAVED_SIGN_INS in password)

        val memory = SettingsHomeUiState(query = "Memory").matching(searchTextOf)
        assertTrue(SettingsSection.YOU in memory || SettingsSection.MEMORY in memory)
        assertTrue(SettingsSection.MEMORY in memory)

        val skills = SettingsHomeUiState(query = "skills").matching(searchTextOf)
        assertTrue(SettingsSection.WHAT_TAFFY_CAN_DO in skills)
        assertTrue(SettingsSection.TAFFY in skills)

        val you = SettingsHomeUiState(query = "You").matching(searchTextOf)
        assertTrue(SettingsSection.YOU in you)

        val library = SettingsHomeUiState(query = "Library").matching(searchTextOf)
        assertTrue(SettingsSection.LIBRARY in library)
    }

    @Test
    fun `history is a settings miss`() {
        assertTrue(SettingsHomeUiState(query = "history").matching(searchTextOf).isEmpty())
    }

    @Test
    fun `a query that matches nothing returns nothing`() {
        assertTrue(SettingsHomeUiState(query = "printer").matching(searchTextOf).isEmpty())
    }

    @Test
    fun `search hits carry destinations for you memory sign-ins skills taffy and library`() {
        val hits = SettingsSearchIndex.hits(
            query = "password",
            sections = SettingsSection.entries,
            searchTextOf = searchTextOf,
            titleOf = { it.name },
            summaryOf = { it.name },
        )
        val destinations = hits.map { it.destination }.toSet()
        assertTrue(TaffyDestination.You in destinations)
        assertTrue(TaffyDestination.SavedSignIns in destinations)
        assertFalse(
            SettingsHomeUiState(query = "password").showsCategoryGrid(compact = true),
        )
    }

    @Test
    fun `every section the home lists has somewhere to go`() {
        val destinations = SettingsSection.entries
            .filter { it.listedOnHome }
            .map { it.destination.route }
        assertEquals(destinations.size, destinations.toSet().size)
        assertTrue(destinations.all { it.isNotBlank() })
    }
}
