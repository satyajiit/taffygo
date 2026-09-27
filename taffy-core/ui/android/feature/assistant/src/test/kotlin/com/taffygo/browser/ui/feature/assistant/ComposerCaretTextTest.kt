// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.MAX_COMPOSER_PREFIX_BYTES
import taffy.core_api.MAX_COMPOSER_SUFFIX_BYTES

/**
 * The two halves that are sent, and the bounds on them.
 *
 * The bounds are the contract's own constants rather than numbers written here,
 * so a contract that changes them changes what this suite asserts rather than
 * disagreeing with it.
 */
class ComposerCaretTextTest {

    @Test
    fun `a caret at the end sends everything before it and no suffix`() {
        val split = ComposerCaretText.at("is this fee refundable?", "is this fee refundable?".length)

        assertEquals("is this fee refundable?", split.prefix)
        assertNull(split.suffix)
    }

    @Test
    fun `a caret in the middle sends both halves`() {
        val split = ComposerCaretText.at("is this fee refundable?", "is this fee".length)

        assertEquals("is this fee", split.prefix)
        assertEquals(" refundable?", split.suffix)
    }

    @Test
    fun `a caret at the start sends an empty prefix and the whole suffix`() {
        val split = ComposerCaretText.at("refundable?", 0)

        assertEquals("", split.prefix)
        assertEquals("refundable?", split.suffix)
    }

    @Test
    fun `a caret outside the text is coerced rather than trusted`() {
        assertEquals("abc", ComposerCaretText.at("abc", 99).prefix)
        assertNull(ComposerCaretText.at("abc", 99).suffix)
        assertEquals("", ComposerCaretText.at("abc", -7).prefix)
        assertEquals("abc", ComposerCaretText.at("abc", -7).suffix)
    }

    @Test
    fun `a long prefix is cut from the front, in bytes, between characters`() {
        // "ने" is one character to a person, two UTF-16 units to Kotlin and six
        // UTF-8 bytes on the wire — which is the whole reason a byte bound
        // cannot be checked by counting characters.
        val unit = "ने"
        val text = unit.repeat(2_000)

        val prefix = ComposerCaretText.at(text, text.length).prefix

        // Inside the contract's bound, and as close under it as one more whole
        // character allows: nothing was thrown away that would have fitted.
        assertTrue(prefix.utf8Size() <= MAX_COMPOSER_PREFIX_BYTES)
        assertTrue(prefix.utf8Size() > MAX_COMPOSER_PREFIX_BYTES - unit.utf8Size())
        // A byte bound and not a character one: this text ran out of bytes at
        // well under a third of the character count the number would suggest.
        assertTrue(prefix.length < MAX_COMPOSER_PREFIX_BYTES)
        // Cut from the front, so the words nearest the caret are the ones kept.
        assertTrue(text.endsWith(prefix))
        // And cut between characters, never through the middle of one: every
        // kept character still carries its vowel sign.
        assertEquals(0, prefix.length % unit.length)
        assertEquals(prefix, String(prefix.toByteArray(Charsets.UTF_8), Charsets.UTF_8))
    }

    @Test
    fun `a long suffix is cut from the back, and never through a surrogate pair`() {
        // One code point, two Chars, four bytes. Cutting a UTF-16 string at an
        // arbitrary index is exactly how half of one of these gets sent.
        val unit = "😀"
        val text = unit.repeat(1_000)

        val suffix = requireNotNull(ComposerCaretText.at(text, 0).suffix)

        assertTrue(suffix.utf8Size() <= MAX_COMPOSER_SUFFIX_BYTES)
        assertTrue(suffix.utf8Size() > MAX_COMPOSER_SUFFIX_BYTES - unit.utf8Size())
        // Cut from the back, so what is kept is what stands closest after the
        // caret rather than the end of a long note.
        assertTrue(text.startsWith(suffix))
        assertEquals(0, suffix.length % unit.length)
        // A pair split down the middle would not survive this round trip: a
        // lone surrogate is not text and an encoder replaces it.
        assertEquals(suffix, String(suffix.toByteArray(Charsets.UTF_8), Charsets.UTF_8))
    }

    @Test
    fun `text inside the bounds is sent exactly as it was typed`() {
        val text = "नमस्ते, is this fee refundable? 😀"
        val split = ComposerCaretText.at(text, "नमस्ते,".length)

        assertEquals("नमस्ते,", split.prefix)
        assertEquals(" is this fee refundable? 😀", split.suffix)
    }

    @Test
    fun `both halves of a long question are bounded independently`() {
        val text = "a".repeat(9_000)
        val split = ComposerCaretText.at(text, 5_000)

        assertEquals(MAX_COMPOSER_PREFIX_BYTES, split.prefix.utf8Size())
        assertEquals(MAX_COMPOSER_SUFFIX_BYTES, requireNotNull(split.suffix).utf8Size())
    }

    private fun String.utf8Size(): Int = toByteArray(Charsets.UTF_8).size
}
