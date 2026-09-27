// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting

/** A display-safe UTF-8 prefix that never ends inside a code point. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun boundedDownloadText(value: String, maxBytes: Int): String {
    require(maxBytes >= 0)
    val bounded = StringBuilder(minOf(value.length, maxBytes))
    var index = 0
    var used = 0
    while (index < value.length) {
        val codePoint = value.codePointAt(index)
        val malformed = codePoint in Char.MIN_SURROGATE.code..Char.MAX_SURROGATE.code
        val width = if (malformed) REPLACEMENT_UTF8_BYTES else utf8Width(codePoint)
        if (used + width > maxBytes) break
        when {
            malformed -> bounded.append(REPLACEMENT_CHARACTER)
            unsafeForDisplay(codePoint) -> bounded.append(' ')
            else -> bounded.appendCodePoint(codePoint)
        }
        used += width
        index += Character.charCount(codePoint)
    }
    return bounded.toString()
}

/** True without allocating an encoded copy, stopping as soon as the limit is crossed. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun downloadTextFits(value: String, maxBytes: Int): Boolean {
    require(maxBytes >= 0)
    var index = 0
    var used = 0
    while (index < value.length) {
        val codePoint = value.codePointAt(index)
        used += if (codePoint in Char.MIN_SURROGATE.code..Char.MAX_SURROGATE.code) {
            REPLACEMENT_UTF8_BYTES
        } else {
            utf8Width(codePoint)
        }
        if (used > maxBytes) return false
        index += Character.charCount(codePoint)
    }
    return true
}

private fun utf8Width(codePoint: Int): Int = when {
    codePoint <= 0x7f -> 1
    codePoint <= 0x7ff -> 2
    codePoint <= 0xffff -> 3
    else -> 4
}

private fun unsafeForDisplay(codePoint: Int): Boolean =
    Character.isISOControl(codePoint) || when (Character.getType(codePoint)) {
        Character.FORMAT.toInt(),
        Character.LINE_SEPARATOR.toInt(),
        Character.PARAGRAPH_SEPARATOR.toInt() -> true
        else -> false
    }

private const val REPLACEMENT_CHARACTER = '\uFFFD'
private const val REPLACEMENT_UTF8_BYTES = 3
