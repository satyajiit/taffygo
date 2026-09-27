// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The profile store, and the one-shot migration that is the risky half.
 *
 * Every case here restarts the repository against the same store, because a
 * migration defect is invisible in the process that performs it and shows up
 * on the next start.
 */
class ProfileLocalProfileRepositoryTest {

    @Test
    fun `an untouched store reads as an empty profile`() {
        val store = FakeStore()

        val profile = ProfileLocalProfileRepository(store).profile.value

        assertEquals(LocalProfile(), profile)
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, profile.monogram)
    }

    @Test
    fun `a legacy banner id becomes the avatar`() {
        val store = FakeStore(ProfileLocalProfileRepository.KEY_LEGACY_BANNER_ID to "b7")

        val profile = ProfileLocalProfileRepository(store).profile.value

        assertEquals(LocalAvatar.of("b7"), profile.avatar)
        assertEquals("b7", store.read(ProfileLocalProfileRepository.KEY_AVATAR_TILE_ID))
    }

    /** The migration must consume the key, not merely read it. */
    @Test
    fun `the legacy key is gone after one start`() {
        val store = FakeStore(ProfileLocalProfileRepository.KEY_LEGACY_BANNER_ID to "c3")

        ProfileLocalProfileRepository(store)

        assertNull(store.read(ProfileLocalProfileRepository.KEY_LEGACY_BANNER_ID))
    }

    /**
     * The defect the migration exists to avoid: clearing the picture, then
     * having the pre-profile key put it back on the next start, for ever.
     */
    @Test
    fun `choosing the monogram after migrating survives a restart`() = runTest {
        val store = FakeStore(ProfileLocalProfileRepository.KEY_LEGACY_BANNER_ID to "a1")

        ProfileLocalProfileRepository(store).setAvatar(LocalAvatar.Monogram)

        assertEquals(LocalAvatar.Monogram, ProfileLocalProfileRepository(store).profile.value.avatar)
    }

    @Test
    fun `a legacy id that names no shipped picture is not stored`() {
        val store = FakeStore(ProfileLocalProfileRepository.KEY_LEGACY_BANNER_ID to "z99")

        val profile = ProfileLocalProfileRepository(store).profile.value

        assertEquals(LocalAvatar.Monogram, profile.avatar)
        assertNull(store.read(ProfileLocalProfileRepository.KEY_AVATAR_TILE_ID))
    }

    /** A profile already migrated keeps its own answer over the legacy one. */
    @Test
    fun `an existing avatar wins over a stale legacy key`() {
        val store = FakeStore(
            ProfileLocalProfileRepository.KEY_LEGACY_BANNER_ID to "a1",
            ProfileLocalProfileRepository.KEY_AVATAR_TILE_ID to "c9",
        )

        assertEquals(
            LocalAvatar.of("c9"),
            ProfileLocalProfileRepository(store).profile.value.avatar,
        )
    }

    @Test
    fun `a name is normalized on the way in and survives a restart`() = runTest {
        val store = FakeStore()

        ProfileLocalProfileRepository(store).setDisplayName("   Ada  Lovelace   ")

        val reopened = ProfileLocalProfileRepository(store).profile.value
        assertEquals("Ada  Lovelace", reopened.displayName)
        assertEquals("AL", reopened.monogram)
    }

    @Test
    fun `clearing a name removes the key rather than storing an empty one`() = runTest {
        val store = FakeStore()
        val repository = ProfileLocalProfileRepository(store)

        repository.setDisplayName("Ada")
        repository.setDisplayName("   ")

        assertNull(store.read(ProfileLocalProfileRepository.KEY_DISPLAY_NAME))
        assertNull(ProfileLocalProfileRepository(store).profile.value.displayName)
    }

    private class FakeStore(vararg initial: Pair<String, String>) : LocalProfileStore {
        private val values = initial.toMap().toMutableMap()

        fun read(key: String): String? = values[key]

        override fun getString(key: String): String? = values[key]

        override fun putString(key: String, value: String?) {
            if (value == null) values.remove(key) else values[key] = value
        }

        override fun remove(key: String) {
            values.remove(key)
        }
    }
}
