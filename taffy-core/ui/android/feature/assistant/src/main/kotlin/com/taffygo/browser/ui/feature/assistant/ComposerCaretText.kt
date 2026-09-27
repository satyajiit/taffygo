// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import taffy.core_api.MAX_COMPOSER_PREFIX_BYTES
import taffy.core_api.MAX_COMPOSER_SUFFIX_BYTES

/**
 * What one suggestion would be asked about: the composer's own text, split at
 * the caret and bounded to what the contract carries (decision
 * `docs/decisions/0097-a-composer-suggestion-is-spent-from-the-persons-own-key.md`
 * section 4).
 *
 * Both halves are the composer's text and nothing else — no page content, no
 * history, no workspace facts, no other conversation. There is nowhere on this
 * type for any of that to be carried, which is a stronger statement than a rule
 * a caller has to keep.
 *
 * [suffix] is absent when the caret is at the end of what was typed, which is
 * the fact the contract records rather than an empty string standing in for it.
 * A person who clicks back into the middle of a sentence and keeps going has
 * text after the caret, and a completion that cannot see it is being asked to
 * continue a sentence it has only half of.
 */
data class ComposerCaretText(
    /** What stands before the caret, bounded, keeping the words nearest it. */
    val prefix: String,
    /** What stands after it, bounded the same way, or null when nothing does. */
    val suffix: String?,
) {
    companion object {
        /**
         * Split [text] at [caret] and bound each half.
         *
         * [caret] is the field's own insertion point, so it already falls
         * between characters — Compose moves a caret by grapheme and never
         * lands it inside one. It is coerced into range anyway, because a
         * state built by hand is cheaper to clamp than to trust.
         */
        fun at(text: String, caret: Int): ComposerCaretText {
            val at = caret.coerceIn(0, text.length)
            val after = text.substring(at)
            return ComposerCaretText(
                prefix = ComposerTextBounds.tail(
                    text = text.substring(0, at),
                    maxBytes = MAX_COMPOSER_PREFIX_BYTES,
                ),
                suffix = if (after.isEmpty()) {
                    null
                } else {
                    ComposerTextBounds.head(text = after, maxBytes = MAX_COMPOSER_SUFFIX_BYTES)
                },
            )
        }
    }
}
