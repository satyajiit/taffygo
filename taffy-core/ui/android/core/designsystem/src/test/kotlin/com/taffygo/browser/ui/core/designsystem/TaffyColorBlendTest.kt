// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import com.taffygo.browser.ui.core.designsystem.internal.TaffyColorSchemes
import com.taffygo.browser.ui.core.designsystem.internal.blendTo
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

/**
 * The theme change blends every colour token, not most of them.
 *
 * A token added to `TaffyColors` and forgotten in `blendTo` would keep its
 * outgoing value for the whole change and then snap to the incoming one on the
 * last frame — a defect nobody would find by reading, because the file still
 * compiles and the other thirty tokens still move. Asserting that the blend
 * reproduces each scheme exactly at its own end catches it: a field left out of
 * the `copy` cannot equal the far scheme at a fraction of one.
 */
class TaffyColorBlendTest {

    @Test
    fun `a blend at zero is the scheme it started from`() {
        assertEquals(
            TaffyColorSchemes.light,
            TaffyColorSchemes.light.blendTo(TaffyColorSchemes.dark, 0f),
        )
        assertEquals(
            TaffyColorSchemes.dark,
            TaffyColorSchemes.dark.blendTo(TaffyColorSchemes.light, 0f),
        )
    }

    @Test
    fun `a blend at one is the scheme it is travelling to, token for token`() {
        assertEquals(
            TaffyColorSchemes.dark,
            TaffyColorSchemes.light.blendTo(TaffyColorSchemes.dark, 1f),
        )
        assertEquals(
            TaffyColorSchemes.light,
            TaffyColorSchemes.dark.blendTo(TaffyColorSchemes.light, 1f),
        )
    }

    @Test
    fun `a blend part way is neither end`() {
        val midway = TaffyColorSchemes.light.blendTo(TaffyColorSchemes.dark, 0.5f)
        assertNotEquals(TaffyColorSchemes.light, midway)
        assertNotEquals(TaffyColorSchemes.dark, midway)
    }
}
