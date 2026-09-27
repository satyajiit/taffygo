// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile
import com.taffygo.browser.ui.core.model.storedId
import com.taffygo.browser.ui.core.preferences.LocalProfileRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * This phone's profile, kept in its own small store.
 *
 * Not behind the Core API, and deliberately: a name and a picture are device
 * state that no other process reads, so routing them through the browser
 * would buy a pipe, a contract field and a generation-replay problem in
 * exchange for nothing. See [LocalProfileRepository].
 *
 * The constructor reads once and migrates once, which is why it is cheap
 * enough to do on the way to the first frame.
 */
class ProfileLocalProfileRepository(
    private val store: LocalProfileStore,
) : LocalProfileRepository {
    private val current = MutableStateFlow(migrateAndRead())

    override val profile: StateFlow<LocalProfile> = current.asStateFlow()

    override suspend fun setDisplayName(name: String?) {
        val normalized = LocalProfile.normalizedDisplayName(name)
        store.putString(KEY_DISPLAY_NAME, normalized)
        current.value = current.value.copy(displayName = normalized)
    }

    override suspend fun setAvatar(avatar: LocalAvatar) {
        store.putString(KEY_AVATAR_TILE_ID, avatar.storedId)
        current.value = current.value.copy(avatar = avatar)
    }

    /**
     * Read the profile, carrying a pre-profile `banner_id` across once.
     *
     * The migration deletes the legacy key **in the same pass that reads
     * it**, and that is the whole of why it is safe to run on every start.
     * Leaving the old key behind would make this a rule that keeps applying:
     * a person who migrates with a tile, then chooses the monogram, would
     * have the tile restored the next time the browser opened, for ever,
     * because the legacy key still said so and the new one being empty reads
     * exactly like a profile that has not migrated yet.
     *
     * A legacy id that names no shipped picture resolves to the monogram
     * through [LocalAvatar.of] rather than being stored and failing later.
     */
    private fun migrateAndRead(): LocalProfile {
        val legacy = store.getString(KEY_LEGACY_BANNER_ID)
        if (legacy != null) {
            store.remove(KEY_LEGACY_BANNER_ID)
            if (store.getString(KEY_AVATAR_TILE_ID) == null) {
                store.putString(KEY_AVATAR_TILE_ID, LocalAvatar.of(legacy).storedId)
            }
        }
        return LocalProfile(
            displayName = LocalProfile.normalizedDisplayName(store.getString(KEY_DISPLAY_NAME)),
            avatar = LocalAvatar.of(store.getString(KEY_AVATAR_TILE_ID)),
        )
    }

    companion object {
        /** The store this profile has always lived in. */
        const val PREFS_NAME: String = "taffy_you_profile"

        const val KEY_DISPLAY_NAME: String = "display_name"
        const val KEY_AVATAR_TILE_ID: String = "avatar_tile_id"

        /**
         * What the picture was called before there was a profile to own it.
         *
         * Read and deleted by the migration above. Nothing else may read it:
         * a second reader would be a second answer to "which picture", and
         * the key is gone after the first start anyway.
         */
        const val KEY_LEGACY_BANNER_ID: String = "banner_id"
    }
}
