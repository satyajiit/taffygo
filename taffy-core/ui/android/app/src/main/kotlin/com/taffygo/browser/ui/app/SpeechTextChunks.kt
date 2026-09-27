// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.text.BreakIterator
import java.util.Locale

/** Splits speech without changing text or cutting a grapheme when a safe boundary exists. */
internal fun speechTextChunks(text: String, maxLength: Int): List<String> {
    require(maxLength >= 2) { "A speech chunk must fit one UTF-16 surrogate pair" }
    if (text.isEmpty()) return emptyList()
    val characters = BreakIterator.getCharacterInstance(Locale.ROOT).apply { setText(text) }
    val chunks = mutableListOf<String>()
    var start = 0
    while (start < text.length) {
        val hardEnd = (start + maxLength).coerceAtMost(text.length)
        var end = if (hardEnd == text.length) {
            text.length
        } else {
            characters.preceding(hardEnd + 1)
        }
        if (end <= start) {
            end = safeCodePointEnd(text, start, hardEnd)
        }
        if (end < text.length) {
            val wordBreak = lastWhitespaceEnd(text, start, end, maxLength / 2)
            if (wordBreak > start) end = wordBreak
        }
        chunks += text.substring(start, end)
        start = end
    }
    return chunks
}

private fun safeCodePointEnd(text: String, start: Int, hardEnd: Int): Int {
    if (hardEnd > start && hardEnd < text.length && text[hardEnd - 1].isHighSurrogate()) {
        return hardEnd - 1
    }
    return hardEnd.coerceAtLeast((start + 1).coerceAtMost(text.length))
}

private fun lastWhitespaceEnd(text: String, start: Int, end: Int, searchFloor: Int): Int {
    val floor = (start + searchFloor).coerceAtMost(end)
    for (index in end - 1 downTo floor) {
        if (text[index].isWhitespace()) return index + 1
    }
    return start
}
