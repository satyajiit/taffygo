// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import com.taffygo.browser.ui.core.common.TaffyResult
import kotlinx.coroutines.flow.StateFlow

/**
 * The seam a field value crosses, and the one thing on this layer that is
 * deliberately **not** on [CoreApiClient].
 *
 * Everything on that facade states an intent about the profile, and the answer
 * comes back as the next `CoreStatus` snapshot: a projection, published to
 * every surface and journalled by the browser so a restart can replay what was
 * decided. That is exactly right for a goal, an approval or a route, and it is
 * exactly wrong for what a person types into a form. What they type is a
 * **value** — a card number, a code that arrived by message, a password — and a
 * value belongs in the browser's own vault, minted there and spent once
 * (decision
 * `docs/decisions/0063-a-field-value-is-minted-by-the-browser-and-spent-once.md`).
 * A value that reached the status plane would be projected into a snapshot
 * every surface reads and written into a journal that outlives the task, and no
 * amount of care further up could take it back out again.
 *
 * So this is a second, narrow seam with its own owner. It carries a description
 * *down* and values *up*, and it carries nothing else. Nothing here returns a
 * value, nothing here echoes one back, and the description that comes down
 * names fields — never their contents.
 */
interface TaskInputClient {
    /**
     * Whether a browser vault is behind this seam in this build.
     *
     * False is the closed answer and the default one: a build with no browser
     * process has nowhere to put a value, and a sheet that collected one anyway
     * would be holding it in this process with no way to spend it. Surfaces ask
     * this before they offer to collect anything.
     */
    val isAvailable: Boolean

    /**
     * The form the browser is holding open for this profile, or null.
     *
     * Described rather than classified: [Described.Field.challenge] is the name
     * the browser used, and turning that name into something a surface may draw
     * is a closed decision made one layer up, where an unrecognised one can
     * refuse the whole request instead of quietly dropping a row.
     */
    val described: StateFlow<Described?>

    /**
     * Hand the browser what the person typed, for it to mint and spend.
     *
     * [values] is keyed by `Described.Field.id`. It is passed and forgotten:
     * this seam does not retain it, does not log it, and nothing it returns
     * carries any part of it back. [requestId] is what makes the submission
     * refusable — a request the browser has already closed is refused rather
     * than applied to whatever it is holding now.
     */
    suspend fun submitValues(requestId: String, values: Map<String, String>): TaffyResult<Unit>

    /**
     * Tell the browser the person has finished with a widget on the page.
     *
     * The interactive case has nothing to hand over: the person worked in a
     * frame this process may not reach into, so all that crosses is "done".
     */
    suspend fun completeInteractive(requestId: String): TaffyResult<Unit>

    /**
     * One form exactly as the browser described it.
     *
     * A nested type because it has no life of its own: it exists to be
     * classified, and every reader of this seam classifies it before doing
     * anything else with it.
     */
    data class Described(
        val requestId: String,
        val host: String,
        val fields: List<Field>,
        /** Browser-enforced lifetime beginning when the person confirms. */
        val approvalLifetimeSeconds: Int = 120,
    ) {
        /**
         * One described row.
         *
         * [challenge] is a name rather than an enumeration on purpose — see
         * [described]. [challengeImage] is bytes rather than an address for a
         * different reason: an address would make this process fetch from the
         * site, carrying its cookies to an origin the browser, not this layer,
         * decides about.
         */
        data class Field(
            val id: String,
            val label: String,
            val challenge: String,
            val placeholder: String? = null,
            val sensitive: Boolean = false,
            val challengeImage: ByteArray? = null,
            val highlightLeft: Float = 0f,
            val highlightTop: Float = 0f,
            val highlightRight: Float = 0f,
            val highlightBottom: Float = 0f,
        ) {
            /** Whether the browser described where the widget is at all. */
            val hasHighlight: Boolean
                get() = highlightRight > highlightLeft && highlightBottom > highlightTop

            /**
             * Value equality over the picture's contents, which a data class
             * does not give for an array. Without it the same form arriving
             * twice compares unequal and every surface holding it redraws.
             */
            override fun equals(other: Any?): Boolean {
                if (this === other) return true
                if (other !is Field) return false
                return id == other.id &&
                    label == other.label &&
                    challenge == other.challenge &&
                    placeholder == other.placeholder &&
                    sensitive == other.sensitive &&
                    challengeImage.contentEquals(other.challengeImage) &&
                    highlightLeft == other.highlightLeft &&
                    highlightTop == other.highlightTop &&
                    highlightRight == other.highlightRight &&
                    highlightBottom == other.highlightBottom
            }

            override fun hashCode(): Int {
                var result = id.hashCode()
                result = 31 * result + label.hashCode()
                result = 31 * result + challenge.hashCode()
                result = 31 * result + placeholder.hashCode()
                result = 31 * result + sensitive.hashCode()
                result = 31 * result + challengeImage.contentHashCode()
                result = 31 * result + highlightLeft.hashCode()
                result = 31 * result + highlightTop.hashCode()
                result = 31 * result + highlightRight.hashCode()
                result = 31 * result + highlightBottom.hashCode()
                return result
            }

            override fun toString(): String = "Field(id=$id, challenge=$challenge)"
        }
    }
}
