// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Which colour the status bar stands on, and which cases refuse to guess. */
class PageColorTest {

    @Test
    fun `a tab that has been nowhere has no page colour`() {
        assertNull(
            PageColor.of(
                hasBeenNowhere = true,
                backgroundArgb = OPAQUE_WHITE,
                themeArgb = OPAQUE_BLUE,
                themingAllowed = true,
            ),
        )
    }

    @Test
    fun `an opaque page background is preferred over the theme colour`() {
        assertEquals(
            OPAQUE_WHITE,
            PageColor.of(
                hasBeenNowhere = false,
                backgroundArgb = OPAQUE_WHITE,
                themeArgb = OPAQUE_BLUE,
                themingAllowed = true,
            ),
        )
    }

    @Test
    fun `a transparent background falls through to an allowed theme colour`() {
        assertEquals(
            OPAQUE_BLUE,
            PageColor.of(
                hasBeenNowhere = false,
                backgroundArgb = TRANSPARENT,
                themeArgb = OPAQUE_BLUE,
                themingAllowed = true,
            ),
        )
    }

    @Test
    fun `a theme colour that is not allowed is not a colour`() {
        assertNull(
            PageColor.of(
                hasBeenNowhere = false,
                backgroundArgb = TRANSPARENT,
                themeArgb = OPAQUE_BLUE,
                themingAllowed = false,
            ),
        )
    }

    @Test
    fun `a transparent theme colour is not a colour either`() {
        assertNull(
            PageColor.of(
                hasBeenNowhere = false,
                backgroundArgb = TRANSPARENT,
                themeArgb = TRANSPARENT,
                themingAllowed = true,
            ),
        )
    }

    private companion object {
        const val OPAQUE_WHITE = 0xFFFFFFFF.toInt()
        const val OPAQUE_BLUE = 0xFF1A73E8.toInt()
        const val TRANSPARENT = 0x00FFFFFF
    }
}
