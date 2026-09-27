// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Chapter markers are already a string. This is the one place "01" is minted,
 * so a settings hub and a You hub cannot drift.
 */
class TaffyChapterIndexTest {

    @Test
    fun `single digits carry a leading zero`() {
        assertEquals("01", taffyChapterIndex(1))
        assertEquals("02", taffyChapterIndex(2))
        assertEquals("03", taffyChapterIndex(3))
        assertEquals("09", taffyChapterIndex(9))
    }

    @Test
    fun `two digits stay two digits`() {
        assertEquals("10", taffyChapterIndex(10))
        assertEquals("12", taffyChapterIndex(12))
    }
}
