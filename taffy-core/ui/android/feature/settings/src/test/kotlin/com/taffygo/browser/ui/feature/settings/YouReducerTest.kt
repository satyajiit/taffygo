// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile
import com.taffygo.browser.ui.core.model.Tab
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-410's projection, its one field, and its navigation no-ops. */
class YouReducerTest {

    private fun project(
        profile: LocalProfile = LocalProfile(),
        tabs: List<Tab> = emptyList(),
        time: TimeOnSitesRepository.Snapshot = YouFixtures.timeUnavailable,
        memory: MemoryRepository.Snapshot = YouFixtures.memoryEmpty,
        signIns: SavedSignInsRepository.Snapshot = YouFixtures.signInsUnavailable,
        details: SavedDetailsRepository.Snapshot = YouFixtures.detailsEmpty,
        detailsOpen: Boolean = false,
        nameDraft: String? = null,
    ) = projectYou(
        time = time,
        memory = memory,
        signIns = signIns,
        details = details,
        tabs = tabs,
        profile = profile,
        detailsOpen = detailsOpen,
        nameDraft = nameDraft,
    )

    @Test
    fun `opening a row leaves the hub state to the navigator`() {
        val state = YouUiState(displayName = "Priya")
        assertEquals(state, reduceYou(state, YouIntent.Open(YouRow.MEMORY)))
        assertEquals(state, reduceYou(state, YouIntent.ChooseAvatar(LocalAvatar.of("a1"))))
    }

    @Test
    fun `the profile row opens and closes the details pane`() {
        val state = YouUiState()
        val open = reduceYou(state, YouIntent.Open(YouRow.PROFILE))
        assertTrue(open.detailsOpen)
        assertTrue(reduceYou(state, YouIntent.OpenDetails).detailsOpen)
        assertFalse(reduceYou(open, YouIntent.CloseDetails).detailsOpen)
    }

    /**
     * Closing the pane is where a typed name is most easily lost, because it
     * is the one exit that is not a decision about the name. The reducer must
     * therefore carry the draft out of the pane rather than drop it; the view
     * model is what stores it.
     */
    @Test
    fun `nothing in the reducer discards a typed name`() {
        val typed = reduceYou(YouUiState(detailsOpen = true), YouIntent.EditName("Ada K"))
        assertEquals("Ada K", typed.nameDraft)
        assertEquals("Ada K", reduceYou(typed, YouIntent.CloseDetails).nameDraft)
        assertEquals("Ada K", reduceYou(typed, YouIntent.CommitName).nameDraft)
        assertEquals(
            "Ada K",
            reduceYou(typed, YouIntent.ChooseAvatar(LocalAvatar.of("b2"))).nameDraft,
        )
    }

    /**
     * The field shows the draft verbatim. The store trims and bounds what it
     * is given, so a field fed from the store would swallow the space between
     * a first and a last name as it was typed — and a two-word name could
     * never be entered at all.
     */
    @Test
    fun `the field shows what was typed, not what was stored`() {
        val stored = project(profile = YouFixtures.named)
        assertEquals("Priya Sharma", youNameField(stored))

        val typing = project(profile = YouFixtures.named, nameDraft = "Ada ")
        assertEquals("Ada ", youNameField(typing))
        assertEquals("Priya Sharma", typing.displayName)
    }

    /** The field refuses the forty-first letter; it does not lose it later. */
    @Test
    fun `the field stops at the length the store would keep`() {
        val long = "b".repeat(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS + 9)
        val typed = reduceYou(YouUiState(), YouIntent.EditName(long))
        assertEquals(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS, typed.nameDraft?.length)
        assertEquals(typed.nameDraft, LocalProfile.normalizedDisplayName(long))
    }

    @Test
    fun `an empty field is a profile with no name, not an error`() {
        assertNull(youShownName(project()))
        assertEquals("", youNameField(project()))
        assertNull(youShownName(project(nameDraft = "   ")))
    }

    /** The picture a person is choosing must match the name they are typing. */
    @Test
    fun `the monogram follows the field while a name is being typed`() {
        val typing = project(profile = YouFixtures.named, nameDraft = "Ada Lovelace")
        assertEquals("AL", typing.monogram)
        assertEquals("Ada Lovelace", youShownName(typing))

        val cleared = project(profile = YouFixtures.named, nameDraft = "")
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, cleared.monogram)
    }

    @Test
    fun `a private tab disables Memory, sign-ins, and Library at the hub`() {
        val state = project(
            profile = YouFixtures.named,
            time = YouFixtures.timeReady,
            memory = YouFixtures.memoryEmpty.copy(notes = YouFixtures.memoryNotes),
            details = YouFixtures.detailsEmpty.copy(people = listOf(YouFixtures.person)),
            tabs = listOf(YouFixtures.tab(private = true)),
        )
        assertTrue(state.privateTab)
        assertEquals("Priya Sharma", state.displayName)
        assertTrue(state.timeHasSites)
        assertEquals(2, state.memoryCount)
        assertEquals(1, state.detailsCount)
        assertFalse(youRowEnabled(YouRow.MEMORY, state))
        assertFalse(youRowEnabled(YouRow.SAVED_SIGN_INS, state))
        assertTrue(youRowEnabled(YouRow.PROFILE, state))
        assertTrue(youRowEnabled(YouRow.TIME_ON_SITES, state))
    }

    @Test
    fun `unavailable time never reports sites`() {
        val state = project(
            time = YouFixtures.timeUnavailable.copy(today = YouFixtures.timeReady.today),
        )
        assertEquals(YouSurfaceAvailability.UNAVAILABLE, state.timeAvailability)
        assertTrue(state.timeHasSites)
    }

    @Test
    fun `the chosen face and its letters reach the hub`() {
        val chosen = project(
            profile = LocalProfile(displayName = "Priya Sharma", avatar = LocalAvatar.of("c4")),
        )
        assertEquals(LocalAvatar.of("c4"), chosen.avatar)
        assertEquals("PS", chosen.monogram)
        assertFalse(chosen.detailsOpen)
    }

    @Test
    fun `a phone with no profile draws the fallback letter and no name`() {
        val empty = project()
        assertEquals(LocalAvatar.Monogram, empty.avatar)
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, empty.monogram)
        assertNull(empty.displayName)
    }

    @Test
    fun `every hub row has a destination`() {
        val routes = YouRow.entries.map { it.destination.route }
        assertEquals(YouRow.entries.size, routes.toSet().size)
        assertTrue(routes.all { it.isNotBlank() })
        assertEquals(YouRow.PROFILE, YouRow.entries.first())
    }
}
