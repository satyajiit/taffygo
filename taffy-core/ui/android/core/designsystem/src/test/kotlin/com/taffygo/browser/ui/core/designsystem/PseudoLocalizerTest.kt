// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Parity row PAR-L10N-001's second half: the pseudo-localization is real, and
 * it is safe to run over every string the UI host ships.
 */
class PseudoLocalizerTest {

    @Test
    fun `a transformed string is bracketed at both ends`() {
        val transformed = PseudoLocalizer.transform("Done")

        assertTrue(transformed, transformed.startsWith("⟦"))
        assertTrue(transformed, transformed.endsWith("⟧"))
    }

    @Test
    fun `letters are accented so untransformed text stands out`() {
        val transformed = PseudoLocalizer.transform("Paused")

        assertNotEquals("Paused", transformed)
        assertTrue(transformed, transformed.contains("Pàüséd"))
    }

    @Test
    fun `the string grows, so a layout that only fits English fails here`() {
        val source = "Taffy needs your OK to open more tabs"
        val transformed = PseudoLocalizer.transform(source)

        val letters = source.count { it.isLetter() }
        val grown = transformed.count { it == '·' }
        assertEquals(Math.round(letters * PseudoLocalizer.EXPANSION).toInt(), grown)
    }

    @Test
    fun `positional format arguments survive untouched`() {
        val transformed = PseudoLocalizer.transform("Done — %1\$d sources, %2\$d conflicts")

        assertTrue(transformed, transformed.contains("%1\$d"))
        assertTrue(transformed, transformed.contains("%2\$d"))
    }

    @Test
    fun `an empty string is left alone`() {
        assertEquals("", PseudoLocalizer.transform(""))
    }

    @Test
    fun `punctuation and digits are not accented`() {
        val transformed = PseudoLocalizer.transform("3 of 4 — 100%%")

        assertTrue(transformed, transformed.contains("3 öf 4 — 100%%"))
    }
}
