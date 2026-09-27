// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * Who this phone's profile is, as the product knows it.
 *
 * Everything here was typed or tapped by the person on this device and is
 * stored nowhere else. There is no account id, address, plan or standing,
 * because TaffyGo holds no account (decision
 * `docs/decisions/0200-taffygo-operates-no-servers.md`) — and no field here
 * is a stand-in for one. An empty profile is complete: the browser works
 * identically whether or not a name was ever given.
 */
data class LocalProfile(
    /**
     * What the person calls themselves, or null if they never said.
     *
     * Blank is not a name. Whitespace-only input stores as null rather than
     * as an empty string, so "has a name" is one question with one answer
     * instead of two that can disagree.
     */
    val displayName: String? = null,

    /** The face, defaulting to initials over the brand sweep. */
    val avatar: LocalAvatar = LocalAvatar.Monogram,
) {
    /**
     * One or two letters for the monogram.
     *
     * Read off the name when there is one, so it changes as the person
     * types. Two initials need two words; a single word gives one letter
     * rather than its first two, because "Sa" reads as a truncation while
     * "S" reads as a monogram.
     *
     * [MONOGRAM_FALLBACK] is what a nameless profile wears. It is the
     * product's own initial, not a question mark or a silhouette: a profile
     * with no name is unremarkable and should not be drawn as missing.
     * Digits count, because a name may legitimately start with one and
     * refusing it would silently give that person the fallback for ever.
     */
    val monogram: String
        get() {
            val words = displayName.orEmpty()
                .split(WHITESPACE)
                .mapNotNull { word -> word.firstOrNull(Char::isLetterOrDigit) }
            return when (words.size) {
                0 -> MONOGRAM_FALLBACK
                1 -> words[0].uppercase()
                else -> "${words.first()}${words.last()}".uppercase()
            }
        }

    companion object {
        /** The monogram a profile with no usable name draws. */
        const val MONOGRAM_FALLBACK: String = "T"

        /**
         * The longest name that may be stored, counted in code points.
         *
         * A bound is needed because this is drawn in fixed-width furniture,
         * and code points rather than `length` because a name built from
         * characters outside the basic plane would otherwise be cut at half
         * its apparent size.
         */
        const val MAX_DISPLAY_NAME_CODE_POINTS: Int = 40

        /**
         * A name as it will be stored: trimmed, bounded, null when empty.
         *
         * The one place that decides, so a name typed on first run and a name
         * edited in settings cannot be stored by two different rules.
         */
        fun normalizedDisplayName(raw: String?): String? {
            val trimmed = raw?.trim().orEmpty()
            if (trimmed.isEmpty()) return null
            val points = trimmed.codePointCount(0, trimmed.length)
            if (points <= MAX_DISPLAY_NAME_CODE_POINTS) return trimmed
            val end = trimmed.offsetByCodePoints(0, MAX_DISPLAY_NAME_CODE_POINTS)
            return trimmed.substring(0, end).trim().ifEmpty { null }
        }

        /**
         * A name as a field may hold it: bounded, never trimmed, never null.
         *
         * A field bounded here stops taking characters in front of the
         * person. Leaving the bound to [normalizedDisplayName] alone would
         * let them type past it and lose the tail at the moment they saved,
         * with nothing on screen having said so.
         */
        fun boundedDisplayName(raw: String): String {
            val points = raw.codePointCount(0, raw.length)
            if (points <= MAX_DISPLAY_NAME_CODE_POINTS) return raw
            return raw.substring(0, raw.offsetByCodePoints(0, MAX_DISPLAY_NAME_CODE_POINTS))
        }

        private val WHITESPACE = Regex("\\s+")
    }
}
