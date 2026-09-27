// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The last refusal one provider answered with, and when.
 *
 * A fact about a request that was made, never about the credential itself: a
 * key that is refused for a rate limit is a key that works. The moment is the
 * core's monotonic reading rather than a wall clock, so it can be compared with
 * other core-published moments and can never be rendered as a date.
 */
data class RosterRefusalState(
    /** What the provider refused for. */
    val refusal: RosterProviderRefusal,
    /** When the core recorded it, on the core's own monotonic clock. */
    val atMonotonicMs: ULong,
    /**
     * When this profile first saw this refusal, as a wall-clock reading, or
     * null where nothing has stamped it.
     *
     * A surface can say "a few minutes ago" with this and cannot with
     * [atMonotonicMs], whose zero is the core process's start. The pure
     * projection leaves it null; the profile-owned repository stamps it on
     * first sight of a given [atMonotonicMs] and keeps that stamp while the
     * same refusal stands, so a snapshot republished for another reason does
     * not make an old refusal read as new.
     */
    val observedAtEpochMillis: Long? = null,
)
