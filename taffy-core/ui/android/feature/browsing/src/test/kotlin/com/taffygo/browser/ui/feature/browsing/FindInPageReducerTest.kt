// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Find in page (SCR-106): query, count, and close. */
class FindInPageReducerTest {

    @Test
    fun `opening starts empty, with no count to show`() {
        val state = openFindInPage(available = false)

        assertTrue(state.open)
        assertEquals("", state.query)
        assertFalse(state.showsCount)
        assertFalse(state.canMove)
        assertFalse(state.available)
    }

    @Test
    fun `an empty query shows no count`() {
        val open = openFindInPage(available = true)
        val state = reduceFindInPage(open, FindInPageIntent.QueryChanged(""))

        assertFalse(state.showsCount)
        assertEquals(0, state.matchCount)
        assertFalse(state.canMove)
    }

    @Test
    fun `no matches is 0 of 0, and next stays disabled`() {
        val open = openFindInPage(available = true)
        val state = reduceFindInPage(
            open,
            FindInPageIntent.QueryChanged("policy"),
            activeIndex = 0,
            matchCount = 0,
        )

        assertTrue(state.showsCount)
        assertEquals(0, state.activeIndex)
        assertEquals(0, state.matchCount)
        assertFalse(state.canMove)
    }

    @Test
    fun `an unavailable engine still reports the zeros it was given`() {
        val open = openFindInPage(available = false)
        val typed = reduceFindInPage(
            open,
            FindInPageIntent.QueryChanged("policy"),
            activeIndex = 0,
            matchCount = 0,
        )

        assertFalse(typed.available)
        assertFalse(typed.canMove)
        assertEquals(0, typed.matchCount)
    }

    @Test
    fun `closing forgets the query`() {
        val open = openFindInPage(available = true)
        val typed = reduceFindInPage(
            open,
            FindInPageIntent.QueryChanged("policy"),
            activeIndex = 1,
            matchCount = 3,
        )
        val closed = reduceFindInPage(typed, FindInPageIntent.Close)

        assertFalse(closed.open)
        assertEquals("", closed.query)
        assertEquals(0, closed.matchCount)
        assertTrue(closed.available)
    }

    @Test
    fun `next carries the engine's new index`() {
        val open = openFindInPage(available = true)
        val typed = reduceFindInPage(
            open,
            FindInPageIntent.QueryChanged("policy"),
            activeIndex = 1,
            matchCount = 3,
        )
        val next = reduceFindInPage(
            typed,
            FindInPageIntent.Next,
            activeIndex = 2,
            matchCount = 3,
        )

        assertTrue(next.canMove)
        assertEquals(2, next.activeIndex)
        assertEquals(3, next.matchCount)
        assertEquals("policy", next.query)
    }
}
