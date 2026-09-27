// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-007's two answers, and what happens to them on the way out. */
class GetStartedReducerTest {

    @Test
    fun `a first run starts on the monogram with no name`() {
        val start = GetStartedUiState()
        assertEquals("", start.name)
        assertEquals(LocalAvatar.Monogram, start.avatar)
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, start.monogram)
        assertNull(getStartedStoredName(start))
        assertFalse(start.dataSheetOpen)
        assertFalse(start.continuing)
    }

    /**
     * The field holds what was typed. The store trims, so a field fed back
     * from it would swallow the space between two words as they were typed
     * and no two-word name could be entered at all.
     */
    @Test
    fun `a name is held as typed, not as it will be stored`() {
        val typed = reduceGetStarted(GetStartedUiState(), GetStartedIntent.EditName("Ada "))
        assertEquals("Ada ", typed.name)
        assertEquals("Ada", getStartedStoredName(typed))
    }

    /** The field refuses the forty-first letter; it does not lose it on save. */
    @Test
    fun `the field stops at the length the store would keep`() {
        val long = "c".repeat(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS + 7)
        val typed = reduceGetStarted(GetStartedUiState(), GetStartedIntent.EditName(long))
        assertEquals(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS, typed.name.length)
        assertEquals(typed.name, getStartedStoredName(typed))
    }

    @Test
    fun `a name of spaces is no name at all`() {
        val spaces = reduceGetStarted(GetStartedUiState(), GetStartedIntent.EditName("   "))
        assertEquals("   ", spaces.name)
        assertNull(getStartedStoredName(spaces))
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, spaces.monogram)
    }

    /** The picture being built has to match the name being typed. */
    @Test
    fun `the monogram follows the name as it is typed`() {
        var state = GetStartedUiState()
        state = reduceGetStarted(state, GetStartedIntent.EditName("A"))
        assertEquals("A", state.monogram)
        state = reduceGetStarted(state, GetStartedIntent.EditName("Ada Lovelace"))
        assertEquals("AL", state.monogram)
        state = reduceGetStarted(state, GetStartedIntent.EditName(""))
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, state.monogram)
    }

    @Test
    fun `choosing a face replaces the one before it and keeps the name`() {
        val named = reduceGetStarted(GetStartedUiState(), GetStartedIntent.EditName("Priya"))
        val first = reduceGetStarted(named, GetStartedIntent.ChooseAvatar(LocalAvatar.of("a1")))
        val second = reduceGetStarted(first, GetStartedIntent.ChooseAvatar(LocalAvatar.of("c9")))
        assertEquals(LocalAvatar.of("c9"), second.avatar)
        assertEquals("Priya", second.name)
        val back = reduceGetStarted(second, GetStartedIntent.ChooseAvatar(LocalAvatar.Monogram))
        assertEquals(LocalAvatar.Monogram, back.avatar)
    }

    @Test
    fun `the data sheet opens and closes without touching the answers`() {
        val answered = reduceGetStarted(
            reduceGetStarted(GetStartedUiState(), GetStartedIntent.EditName("Priya")),
            GetStartedIntent.ChooseAvatar(LocalAvatar.of("b3")),
        )
        val open = reduceGetStarted(answered, GetStartedIntent.OpenDataSheet)
        assertTrue(open.dataSheetOpen)
        val closed = reduceGetStarted(open, GetStartedIntent.CloseDataSheet)
        assertFalse(closed.dataSheetOpen)
        assertEquals(answered, closed)
    }

    /**
     * Continue latches. The writes and the navigation are one step, so a
     * second tap part-way through would either store the answers twice or
     * push AI setup twice, and a person tapping a large button on a slow
     * phone taps it twice.
     */
    @Test
    fun `continue latches and nothing moves after it`() {
        val answered = reduceGetStarted(GetStartedUiState(), GetStartedIntent.EditName("Priya"))
        val going = reduceGetStarted(answered, GetStartedIntent.Continue)
        assertTrue(going.continuing)
        assertEquals(going, reduceGetStarted(going, GetStartedIntent.Continue))
        assertEquals(going, reduceGetStarted(going, GetStartedIntent.EditName("Someone else")))
        assertEquals(
            going,
            reduceGetStarted(going, GetStartedIntent.ChooseAvatar(LocalAvatar.of("a2"))),
        )
        assertEquals("Priya", going.name)
    }
}
