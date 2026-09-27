// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import kotlinx.coroutines.flow.StateFlow

/**
 * One Chromium regular-profile store, projected without filesystem paths.
 *
 * Two screens read it: SCR-708 lists profiles and deletes one an earlier build
 * left behind, and SCR-304 names the profile its workspaces live in, because a
 * workspace is kept inside the profile that made it (decision
 * `docs/decisions/0102-a-workspace-belongs-to-the-profile-that-made-it.md`).
 * That is why the seam sits in this module rather than in the settings feature.
 *
 * [create] and [activate] stay on the port and no screen calls them: 1.0 ships
 * one browser profile (decision
 * `docs/decisions/0255-one-browser-profile-in-1-0.md`), because Chromium on
 * Android builds startup data for the first profile only.
 */
interface BrowserProfilesRepository {
    val snapshot: StateFlow<Snapshot>

    fun refresh()

    suspend fun create(displayName: String): Result

    suspend fun activate(profileId: String): Result

    suspend fun delete(profileId: String): Result

    data class Profile(
        val id: String,
        val displayName: String,
        val active: Boolean,
        /**
         * Whether the engine chose [displayName] rather than a person. Chromium
         * names the first profile "Your Chromium"; the implementation replaces
         * such a name with the product's own before a screen reads it
         * (decision 0255).
         */
        val usesDefaultName: Boolean = false,
    )

    data class Snapshot(
        val availability: Availability = Availability.LOADING,
        val profiles: List<Profile> = emptyList(),
    )

    data class Result(
        val failure: Failure? = null,
    ) {
        val succeeded: Boolean
            get() = failure == null
    }

    enum class Availability {
        LOADING,
        READY,
        UNAVAILABLE,
    }

    enum class Failure {
        UNAVAILABLE,
        PRIVATE_PROFILE,
        NOT_ACTIVE,
        INVALID_NAME,
        LIMIT_REACHED,
        DUPLICATE_NAME,
        NOT_FOUND,
        ACTIVE_PROFILE,
        LAST_PROFILE,
        PROFILE_IN_USE,
        BUSY,
        FAILED,
    }
}
