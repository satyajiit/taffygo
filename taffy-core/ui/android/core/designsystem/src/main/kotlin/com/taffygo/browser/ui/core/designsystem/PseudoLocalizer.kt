// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

/**
 * Pseudo-localization, applied at runtime (parity row PAR-L10N-001).
 *
 * The debug package also carries the build-time pseudo-locales, which need a
 * device language change to see. This is the same transformation available
 * without one, so a reviewer can turn it on in Appearance and walk every screen
 * looking for the two things it exposes: text that never left the source, and
 * layouts that only fit English.
 *
 * The three rules are the standard ones. Brackets mark the ends of a string, so
 * a truncation is visible. Letters gain accents, so text that comes from a
 * resource is obvious at a glance and text that does not is obvious too.
 * Length grows by [EXPANSION], because most translations are longer than
 * English and a layout that only fits English fails here rather than in a
 * translated build. Format arguments are copied through untouched: a
 * transformation that corrupted `%1$s` would be testing itself, not the string.
 */
object PseudoLocalizer {

    /** How much longer the transformed string is, as a fraction. */
    const val EXPANSION: Double = 0.3

    /** The transformed form of [source]. */
    fun transform(source: String): String {
        if (source.isEmpty()) return source
        val accented = StringBuilder()
        var index = 0
        while (index < source.length) {
            val argument = FORMAT_ARGUMENT.matchAt(source, index)
            if (argument != null) {
                accented.append(argument.value)
                index += argument.value.length
            } else {
                accented.append(accent(source[index]))
                index++
            }
        }
        return "$OPEN$accented${padding(source)}$CLOSE"
    }

    private fun padding(source: String): String {
        val letters = source.count { it.isLetter() }
        val added = Math.round(letters * EXPANSION).toInt()
        return if (added <= 0) "" else " " + PAD.repeat(added)
    }

    private fun accent(character: Char): Char {
        val index = ACCENTABLE.indexOf(character)
        return if (index < 0) character else ACCENTED[index]
    }

    private const val OPEN = "⟦"
    private const val CLOSE = "⟧"
    private const val PAD = "·"

    private const val ACCENTABLE = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    private const val ACCENTED = "àbçdéfghîjklmñöpqrstüvwxyzÀBÇDÉFGHÎJKLMÑÖPQRSTÜVWXYZ"

    /** A positional or plain format argument, copied through unchanged. */
    private val FORMAT_ARGUMENT = Regex("%(?:\\d+\\\$)?[-#+ 0,(]*\\d*(?:\\.\\d+)?[sdfxSDFX%]")
}
