// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HistoryVisitTest {
    @Test
    fun `stored identity survives refresh and hides the address`() {
        val address = "https://example.test/account?view=recent"
        val first = HistoryVisit.Id.fromStoredVisit(address, 1_788_000_000_123L)
        val refreshed = HistoryVisit.Id.fromStoredVisit(address, 1_788_000_000_123L)

        assertEquals(first, refreshed)
        assertEquals(72, first.value.length)
        assertTrue(first.value.matches(Regex("history-[0-9a-f]{64}")))
        assertFalse(first.value.contains("example"))
        assertFalse(first.value.contains("account"))
    }

    @Test
    fun `address and visit time both participate in identity`() {
        val base = HistoryVisit.Id.fromStoredVisit("https://one.test/page", 10L)
        val anotherAddress = HistoryVisit.Id.fromStoredVisit("https://two.test/page", 10L)
        val anotherVisit = HistoryVisit.Id.fromStoredVisit("https://one.test/page", 11L)

        assertNotEquals(base, anotherAddress)
        assertNotEquals(base, anotherVisit)
        assertNotEquals(anotherAddress, anotherVisit)
    }
}
