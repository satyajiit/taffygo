// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One row of a form Taffy is holding open, classified.
 *
 * Every row is described by the browser and drawn from that description: the
 * label, the keyboard, the masking and the challenge all arrive as data. No
 * surface carries a list of field names, because the next site has different
 * ones and a compiled-in list is a product that works on one site.
 *
 * [label] and [placeholder] are the site's own words rather than the product's,
 * which is why they are values here and not string resources: nothing compiled
 * in can name a field a page invented.
 */
class TaskInputField(
    /** Opaque identity the browser gave this field. Never shown to a person. */
    val id: String,
    /** What to call the field, as the page called it. */
    val label: String,
    /** A hint under the field, when the page offered one. */
    val placeholder: String? = null,
    /**
     * Whether what is typed here must not be legible on screen.
     *
     * Masking is the surface's half of it. The other half is that a value never
     * leaves the sheet's own memory — see the sheet's view model — and never
     * reaches the status plane, which is projected and journalled.
     */
    val sensitive: Boolean = false,
    /** What kind of field this is. */
    val challenge: TaskChallengeKind = TaskChallengeKind.NONE,
    /**
     * The picture for an [TaskChallengeKind.IMAGE_CHALLENGE], as bytes the
     * browser already fetched.
     *
     * Bytes rather than an address, on purpose: an address would make this
     * process fetch from the site, and it has no business talking to the site
     * at all.
     */
    val challengeImage: ByteArray? = null,
    /**
     * Where on the page the widget for an
     * [TaskChallengeKind.INTERACTIVE_CHALLENGE] is, in page fractions.
     *
     * Normalized so nothing here depends on a device, a zoom level or a scroll
     * position. The surface that draws it multiplies by the page area it has.
     */
    val highlight: Highlight? = null,
) {
    /**
     * A rectangle over the page, as fractions of the page area from its top
     * left corner.
     *
     * Every value is clamped into 0..1 on construction rather than trusted,
     * because this arrives from a description of a page and a page is not a
     * party this process trusts. An overlay driven by an unclamped fraction
     * draws its cut-out off the screen, which reads as no highlight at all.
     */
    data class Highlight(
        val left: Float,
        val top: Float,
        val right: Float,
        val bottom: Float,
    ) {
        /** The clamped left edge. */
        val leftFraction: Float = left.coerceIn(0f, 1f)

        /** The clamped top edge. */
        val topFraction: Float = top.coerceIn(0f, 1f)

        /** The clamped right edge, never left of [leftFraction]. */
        val rightFraction: Float = right.coerceIn(leftFraction, 1f)

        /** The clamped bottom edge, never above [topFraction]. */
        val bottomFraction: Float = bottom.coerceIn(topFraction, 1f)

        /** Whether this rectangle encloses anything at all. */
        val isEmpty: Boolean
            get() = rightFraction <= leftFraction || bottomFraction <= topFraction
    }

    /**
     * Value equality over the picture's contents, which a data class does not
     * give for an array.
     *
     * Written out for the reason `PartMemberRead` writes it out: the generated
     * equals would compare array identities, so the same form arriving twice
     * would compare unequal and every surface holding it would redraw.
     */
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is TaskInputField) return false
        return id == other.id &&
            label == other.label &&
            placeholder == other.placeholder &&
            sensitive == other.sensitive &&
            challenge == other.challenge &&
            challengeImage.contentEquals(other.challengeImage) &&
            highlight == other.highlight
    }

    override fun hashCode(): Int {
        var result = id.hashCode()
        result = 31 * result + label.hashCode()
        result = 31 * result + placeholder.hashCode()
        result = 31 * result + sensitive.hashCode()
        result = 31 * result + challenge.hashCode()
        result = 31 * result + challengeImage.contentHashCode()
        result = 31 * result + highlight.hashCode()
        return result
    }

    override fun toString(): String = "TaskInputField(id=$id, challenge=$challenge)"
}
