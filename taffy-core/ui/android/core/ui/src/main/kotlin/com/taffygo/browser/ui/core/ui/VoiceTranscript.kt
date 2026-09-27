// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import java.text.BreakIterator
import java.util.Locale

/**
 * A non-blank transcript that fits the largest editor it can enter.
 *
 * Speech services are outside the product's allocation and output controls, so
 * their result is bounded at the seam before Compose can retain or lay it out.
 * Cutting follows grapheme boundaries and never leaves a lone surrogate or a
 * detached Devanagari vowel sign. [wasTruncated] keeps the loss visible in the
 * confirmation overlay instead of silently presenting a partial sentence as
 * the whole thing.
 */
@ConsistentCopyVisibility
data class VoiceTranscript private constructor(
    val text: String,
    val wasTruncated: Boolean,
) {
    /** UI-state diagnostics may name the fact, never a person's recognized words. */
    override fun toString(): String =
        "VoiceTranscript(text=<redacted>, wasTruncated=$wasTruncated)"

    companion object {
        fun bounded(candidate: String): VoiceTranscript? {
            val normalized = candidate.trim()
            if (normalized.isEmpty()) return null
            if (utf8Bytes(normalized, stopAfter = MAX_VOICE_TRANSCRIPT_BYTES) <=
                MAX_VOICE_TRANSCRIPT_BYTES
            ) {
                return VoiceTranscript(normalized, wasTruncated = false)
            }
            val characters = BreakIterator.getCharacterInstance(Locale.ROOT)
            characters.setText(normalized)
            var end = characters.first()
            var next = characters.next()
            var bytes = 0
            while (next != BreakIterator.DONE) {
                val nextBytes = utf8Bytes(normalized, end, next)
                if (bytes + nextBytes > MAX_VOICE_TRANSCRIPT_BYTES) break
                bytes += nextBytes
                end = next
                next = characters.next()
            }
            val bounded = normalized.substring(0, end).trimEnd()
            return bounded.takeIf(String::isNotEmpty)?.let {
                VoiceTranscript(it, wasTruncated = true)
            }
        }
    }
}

/** The portable task goal ceiling; a confirmed transcript cannot exceed it. */
const val MAX_VOICE_TRANSCRIPT_BYTES: Int = 8_192

private fun utf8Bytes(
    text: String,
    from: Int = 0,
    to: Int = text.length,
    stopAfter: Int = Int.MAX_VALUE,
): Int {
    var bytes = 0
    var index = from
    while (index < to) {
        val character = text[index]
        bytes += when {
            character.code < 0x80 -> 1
            character.code < 0x800 -> 2
            character.isHighSurrogate() &&
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
