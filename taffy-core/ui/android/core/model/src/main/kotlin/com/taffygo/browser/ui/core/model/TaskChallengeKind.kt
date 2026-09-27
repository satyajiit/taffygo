// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * What one field of a form is, when it is not plain text a person just types.
 *
 * Closed, like every other enumeration that crosses a seam: a value this
 * product does not know is not a field it may draw a guess at. The three
 * non-plain kinds are here because each one needs the surface to do something
 * different — fetch nothing itself, keep a keyboard numeric, or refuse to draw
 * the widget at all — and a surface cannot make that choice from a string it
 * failed to recognise.
 *
 * [fromWire] answers null rather than a default for exactly that reason, and
 * [TaskInputRequest.fromWire] turns that null into a refused request rather
 * than a request with one field missing. Half a form is worse than no form:
 * the person fills in what they can see, the site rejects it, and nothing on
 * the screen ever says why.
 */
enum class TaskChallengeKind(
    /** The compiled-in name the browser describes this kind by. */
    val wireName: String,
) {
    /** An ordinary field: a label, a keyboard, and what the person types. */
    NONE("none"),

    /**
     * A picture the site wants read back.
     *
     * The bytes travel with the field. The surface never holds a URL for one
     * and never fetches one: a request the UI process made itself would carry
     * that process's cookies to the site, from outside everything the browser
     * decided about the page.
     */
    IMAGE_CHALLENGE("image_challenge"),

    /**
     * A widget the person has to work in on the page itself.
     *
     * It cannot be drawn in a sheet — it lives in a cross-origin frame, and
     * nothing in this process may reach into one — so the sheet collapses to an
     * instruction, a highlight over where it is, and a way to say when it is
     * done.
     */
    INTERACTIVE_CHALLENGE("interactive_challenge"),

    /** A short code that arrived by message. Numeric, and never remembered. */
    ONE_TIME_CODE("one_time_code"),
    ;

    companion object {
        /**
         * The kind this name means, or null when it means nothing here.
         *
         * A linear scan over four members rather than a map, because the map
         * would be the thing to keep in step with the enumeration and the
         * enumeration is already the answer.
         */
        fun fromWire(wireName: String): TaskChallengeKind? =
            entries.firstOrNull { it.wireName == wireName }
    }
}
