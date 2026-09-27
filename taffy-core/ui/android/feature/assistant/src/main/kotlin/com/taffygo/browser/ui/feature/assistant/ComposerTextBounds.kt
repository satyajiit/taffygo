// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import java.text.BreakIterator
import java.util.Locale

/**
 * How the composer's two halves are cut down to what the contract will carry.
 *
 * The bounds are **byte** bounds and a Kotlin string is UTF-16, so the two
 * counts are not the same number and cannot be substituted for one another: a
 * line of Hindi is three bytes per character and an emoji is four, so a prefix
 * runs past 4096 bytes at somewhere over a thousand characters rather than at
 * four thousand. Counting characters here would send a request the core refuses
 * — and refuses correctly, because it measures what the wire carries.
 *
 * Which end is kept is a different decision for each half, and both keep the
 * text nearest the caret, because that is the text the suggestion is about. A
 * prefix is cut from the **front**: the words a person just typed are the ones
 * a completion continues, and the opening of a long note is not. A suffix is
 * cut from the **back**, for the same reason read the other way.
 *
 * Nothing is ever cut in the middle of a character. The unit is a grapheme
 * cluster from [BreakIterator], not a `Char` and not a code point, so a
 * surrogate pair stays whole, a Devanagari consonant keeps its vowel sign, and
 * a family emoji does not become four people. Cutting a UTF-16 string at an
 * arbitrary index is how a lone surrogate reaches an encoder, and a lone
 * surrogate is not text: it encodes as a replacement character, so the bound
 * would be honoured and the last word would still be wrong.
 */
internal object ComposerTextBounds {

    /** Whether [text] fits the wire byte ceiling, without allocating encoded bytes. */
    fun fits(text: String, maxBytes: Int): Boolean =
        utf8Bytes(text, 0, text.length, stopAfter = maxBytes) <= maxBytes

    /**
     * The last whole characters of [text] that fit in [maxBytes] once encoded.
     *
     * Returns [text] itself when it already fits, which is the ordinary case
     * and costs one pass and no allocation.
     */
    fun tail(text: String, maxBytes: Int): String {
        if (fits(text, maxBytes)) return text
        val characters = BreakIterator.getCharacterInstance(Locale.ROOT)
        characters.setText(text)
        var kept = 0
        var cut = characters.last()
        var start = characters.previous()
        while (start != BreakIterator.DONE) {
            kept += utf8Bytes(text, start, cut)
            if (kept > maxBytes) break
            cut = start
            start = characters.previous()
        }
        return text.substring(cut)
    }

    /** The first whole characters of [text] that fit in [maxBytes] once encoded. */
    fun head(text: String, maxBytes: Int): String {
        if (fits(text, maxBytes)) return text
        val characters = BreakIterator.getCharacterInstance(Locale.ROOT)
        characters.setText(text)
        var kept = 0
        var cut = characters.first()
        var end = characters.next()
        while (end != BreakIterator.DONE) {
            kept += utf8Bytes(text, cut, end)
            if (kept > maxBytes) break
            cut = end
            end = characters.next()
        }
        return text.substring(0, cut)
    }

    /**
     * What `[from, to)` of [text] weighs once encoded, without encoding it.
     *
     * A surrogate that stands alone counts as three, because that is what an
     * encoder charges for the replacement character it emits in its place —
     * counting it as half of a pair would let a string past the bound the wire
     * then refuses it for.
     */
    private fun utf8Bytes(
        text: String,
        from: Int,
        to: Int,
        stopAfter: Int = Int.MAX_VALUE,
    ): Int {
        var bytes = 0
        var index = from
        while (index < to) {
            val code = text[index].code
            bytes += when {
                code < 0x80 -> 1
                code < 0x800 -> 2
                text[index].isHighSurrogate() &&
                    index + 1 < to &&
                    text[index + 1].isLowSurrogate() -> {
                    index++
                    4
                }
                else -> 3
            }
            if (bytes > stopAfter) return bytes
            index++
        }
        return bytes
    }
}
