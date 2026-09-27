// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import org.junit.Assert.assertEquals
import org.junit.Test

class PackWebpTest {

    @Test
    fun aPlateDrawnSmallIsDecodedSmall() {
        // The published plate is 640 by 480 and the start page draws it 120 dp
        // wide, so a quarter in each direction still covers what is drawn.
        assertEquals(4, PackWebp.sampleSize(width = 640, targetWidthPx = 120))
        assertEquals(2, PackWebp.sampleSize(width = 640, targetWidthPx = 168))
    }

    @Test
    fun artworkDrawnAtItsOwnSizeIsNotSampled() {
        assertEquals(1, PackWebp.sampleSize(width = 192, targetWidthPx = 192))
        assertEquals(1, PackWebp.sampleSize(width = 192, targetWidthPx = 384))
    }

    @Test
    fun anImpossibleRequestSamplesRatherThanDividingByNothing() {
        assertEquals(1, PackWebp.sampleSize(width = 0, targetWidthPx = 120))
        assertEquals(1, PackWebp.sampleSize(width = 640, targetWidthPx = 0))
        assertEquals(1, PackWebp.sampleSize(width = -640, targetWidthPx = 120))
    }
}
