// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * A committed tile is decoded no larger than the surface that asked for it.
 *
 * The sizes below are the ones the product actually draws: the strip tile and
 * the identity picture. A regression here is not a crash — it is every avatar
 * decoding at full resolution into a 40dp box, which costs memory on a
 * surface that draws thirty of them at once.
 */
class TaffyProfileAvatarTest {

    @Test
    fun `a tile decodes only as large as the surface needs`() {
        assertEquals(8, taffyTileSampleSize(width = 512, height = 512, targetPixels = 64))
        assertEquals(2, taffyTileSampleSize(width = 512, height = 512, targetPixels = 216))
        assertEquals(1, taffyTileSampleSize(width = 512, height = 512, targetPixels = 300))
    }

    /** Degenerate input must answer "decode it whole", never zero or a loop. */
    @Test
    fun `an impossible request samples at one`() {
        assertEquals(1, taffyTileSampleSize(width = 0, height = 512, targetPixels = 64))
        assertEquals(1, taffyTileSampleSize(width = 512, height = 0, targetPixels = 64))
        assertEquals(1, taffyTileSampleSize(width = 512, height = 512, targetPixels = 0))
        assertEquals(1, taffyTileSampleSize(width = -1, height = -1, targetPixels = -1))
    }
}
