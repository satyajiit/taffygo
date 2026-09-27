// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The face a person gives this phone's profile.
 *
 * There is no account, so this is not a picture of anyone and it is never
 * fetched, uploaded or shared: it is a choice stored on the device and drawn
 * from bytes already inside the build. Two kinds, and the distinction is
 * deliberate rather than a default and a special case —
 *
 * [Monogram] is what a profile has before it chooses, and it stays a real
 * answer afterwards. It is drawn from the person's own initials over the
 * brand sweep, so a profile with no picture still looks like a profile
 * rather than like a failed image load.
 *
 * [Tile] is one of the committed pictures, named by id. The **id** is what is
 * persisted, never a path and never a URL, so the bytes behind it can be
 * re-encoded or moved between modules without touching a stored profile.
 */
sealed interface LocalAvatar {

    /** Initials over the brand sweep. The resting state, not a placeholder. */
    data object Monogram : LocalAvatar

    /** One committed picture, by id. Construct through [of] so it is known. */
    data class Tile internal constructor(val id: String) : LocalAvatar

    companion object {
        /**
         * Every picture the build ships, in the order a chooser offers them.
         *
         * Thirty, in three families of ten. The list is the authority for
         * what may be stored: an id absent from it has no bytes behind it,
         * and resolving it would draw nothing.
         */
        val TILE_IDS: List<String> =
            listOf("a", "b", "c").flatMap { family -> (1..10).map { "$family$it" } }

        /**
         * The avatar for a stored id, or [Monogram] when there is none.
         *
         * Unknown is not an error and must not be one. An id can outlive the
         * picture it named — a family withdrawn, a build that ships fewer —
         * and the honest answer then is the monogram the profile started
         * with, not a broken tile and not a crash on a value that was valid
         * when it was written.
         */
        fun of(id: String?): LocalAvatar =
            if (id != null && id in TILE_IDS) Tile(id) else Monogram
    }
}

/** The stored form: a tile id, or null for [LocalAvatar.Monogram]. */
val LocalAvatar.storedId: String?
    get() = when (this) {
        is LocalAvatar.Tile -> id
        LocalAvatar.Monogram -> null
    }
