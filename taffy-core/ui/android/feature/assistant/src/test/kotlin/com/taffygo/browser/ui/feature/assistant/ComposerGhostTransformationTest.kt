// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.input.OffsetMapping
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The offset map under the ghost, tested directly.
 *
 * It is worth its own suite because it is the one piece of this feature that
 * fails by crashing rather than by looking wrong: a text field asks the mapping
 * where a tap landed and where to draw a caret, and an answer outside the text
 * it was asked about takes the field down. Every assertion below is about one
 * of the three properties a mapping has to hold — the two directions are
 * inverse over what the person typed, no answer leaves its own text, and no
 * offset inside the offered characters maps to anything but the caret.
 */
class ComposerGhostTransformationTest {

    private fun ghost(text: String, ghost: String, caret: Int) =
        ComposerGhostTransformation(ghost = ghost, caret = caret, ghostColor = Color.Gray)
            .filter(AnnotatedString(text))

    @Test
    fun `a ghost at the end is drawn after what was typed`() {
        val shown = ghost(text = "the quick br", ghost = "own fox", caret = 12)

        assertEquals("the quick brown fox", shown.text.text)
    }

    @Test
    fun `a ghost at the caret is drawn where the caret is`() {
        val shown = ghost(text = "the quick fox", ghost = " brown", caret = "the quick".length)

        // Not "the quick fox brown": the offer goes in where the person is
        // reading, and the rest of their sentence stays after it.
        assertEquals("the quick brown fox", shown.text.text)
    }

    @Test
    fun `nothing offered is the identity`() {
        val shown = ghost(text = "the quick fox", ghost = "", caret = 4)

        assertEquals("the quick fox", shown.text.text)
        assertEquals(OffsetMapping.Identity, shown.offsetMapping)
    }

    @Test
    fun `every typed offset maps out and back to itself`() {
        val text = "the quick fox"
        val shown = ghost(text = text, ghost = " brown", caret = "the quick".length)

        // The property a field relies on to put the caret where a tap landed.
        for (offset in 0..text.length) {
            assertEquals(
                offset,
                shown.offsetMapping.transformedToOriginal(
                    shown.offsetMapping.originalToTransformed(offset),
                ),
            )
        }
    }

    @Test
    fun `an offset inside the offered characters is clamped back to the caret`() {
        val caret = "the quick".length
        val offered = " brown"
        val shown = ghost(text = "the quick fox", ghost = offered, caret = caret)

        // This is the whole of what keeps the ghost un-editable: there is no
        // drawn position inside it that the caret can be placed at.
        for (inside in 1..offered.length) {
            assertEquals(caret, shown.offsetMapping.transformedToOriginal(caret + inside))
        }
    }

    @Test
    fun `no offset either direction escapes the text it is mapped into`() {
        val text = "the quick fox"
        val offered = " brown"
        val shown = ghost(text = text, ghost = offered, caret = 4)
        val drawn = text.length + offered.length

        for (offset in 0..text.length) {
            val into = shown.offsetMapping.originalToTransformed(offset)
            assertTrue("$offset drew at $into", into in 0..drawn)
        }
        for (offset in 0..drawn) {
            val back = shown.offsetMapping.transformedToOriginal(offset)
            assertTrue("$offset came back as $back", back in 0..text.length)
        }
    }

    @Test
    fun `a caret past the end of the text is clamped rather than trusted`() {
        val shown = ghost(text = "abc", ghost = "def", caret = 99)

        assertEquals("abcdef", shown.text.text)
        assertEquals(3, shown.offsetMapping.originalToTransformed(3))
        assertEquals(3, shown.offsetMapping.transformedToOriginal(6))
    }

    @Test
    fun `a caret before the start of the text is clamped rather than trusted`() {
        val shown = ghost(text = "abc", ghost = "def", caret = -7)

        assertEquals("defabc", shown.text.text)
        // The caret is drawn immediately before what is offered, at either end
        // of the text: the ghost stands after the caret and never behind it.
        assertEquals(0, shown.offsetMapping.originalToTransformed(0))
        assertEquals(4, shown.offsetMapping.originalToTransformed(1))
        assertEquals(0, shown.offsetMapping.transformedToOriginal(2))
    }
}
