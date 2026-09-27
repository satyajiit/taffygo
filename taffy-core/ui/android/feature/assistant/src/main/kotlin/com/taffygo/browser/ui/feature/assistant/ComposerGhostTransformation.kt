// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.withStyle

/**
 * The suggestion drawn at the caret, in the composer's own field.
 *
 * A visual transformation rather than a second control, because ghost text is
 * an offer about the sentence being typed and belongs where that sentence is.
 * The characters are never in the field's value: the person's text is exactly
 * what they typed until they accept, and continuing to type replaces the offer
 * rather than editing it.
 *
 * [caret] is where they go in. It used to be the end of the text and did not
 * need saying; now that the composer knows where its caret is, the offer is
 * drawn where the person is actually reading, and a mid-sentence caret is an
 * insertion rather than an append.
 *
 * [ghost] empty draws nothing at all, which is what both "no suggestion" and "a
 * suggestion of no characters" come to on screen — the difference between those
 * two is kept in [ComposerSuggestion] rather than here, because it is a fact
 * about the answer and not about the drawing.
 */
class ComposerGhostTransformation(
    private val ghost: String,
    private val caret: Int,
    private val ghostColor: Color,
) : VisualTransformation {

    override fun filter(text: AnnotatedString): TransformedText {
        if (ghost.isEmpty()) return TransformedText(text, OffsetMapping.Identity)
        // The field is the authority on its own length, and this transformation
        // is handed a caret from state that a hand-built preview could put
        // anywhere. An offset map that runs past either end is the bug that
        // makes a text field jump or crash on selection, so the clamp is here
        // rather than at each of the callers.
        val at = caret.coerceIn(0, text.length)
        val shown = buildAnnotatedString {
            append(text.subSequence(0, at))
            withStyle(SpanStyle(color = ghostColor)) { append(ghost) }
            append(text.subSequence(at, text.length))
        }
        return TransformedText(shown, CaretGhostOffsets(caret = at, ghostLength = ghost.length))
    }

    override fun equals(other: Any?): Boolean =
        other is ComposerGhostTransformation &&
            other.ghost == ghost &&
            other.caret == caret &&
            other.ghostColor == ghostColor

    override fun hashCode(): Int {
        var result = ghost.hashCode()
        result = 31 * result + caret
        return 31 * result + ghostColor.hashCode()
    }
}

/**
 * Offsets for text with characters drawn into the middle of it.
 *
 * Everything before the caret is itself in both directions. Everything after it
 * is shifted by the length of what was drawn. Every drawn offset that lands
 * *inside* those characters is clamped back to the caret, so the caret can
 * never be placed within the suggestion and no selection can reach into it —
 * that clamp is the whole of what keeps the ghost un-editable, and it is the
 * only rule here that is not arithmetic.
 *
 * The two directions are inverse over what a person typed: mapping any typed
 * offset out and back returns the same offset, which is the property a text
 * field relies on to place a caret where a tap landed.
 */
private class CaretGhostOffsets(
    private val caret: Int,
    private val ghostLength: Int,
) : OffsetMapping {

    override fun originalToTransformed(offset: Int): Int =
        if (offset <= caret) offset else offset + ghostLength

    override fun transformedToOriginal(offset: Int): Int =
        if (offset <= caret) offset else (offset - ghostLength).coerceAtLeast(caret)
}
