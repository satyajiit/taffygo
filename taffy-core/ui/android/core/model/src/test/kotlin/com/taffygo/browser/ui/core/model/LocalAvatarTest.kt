// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * What may be stored as a face, and what an unknown id resolves to.
 *
 * The rule these all serve: an id is only ever a name for bytes this build
 * ships, so an id with nothing behind it must degrade to the monogram rather
 * than being stored and drawn as a hole.
 */
class LocalAvatarTest {

    @Test
    fun `thirty tiles in three families`() {
        assertEquals(30, LocalAvatar.TILE_IDS.size)
        assertEquals(30, LocalAvatar.TILE_IDS.toSet().size)
        assertEquals("a1", LocalAvatar.TILE_IDS.first())
        assertEquals("c10", LocalAvatar.TILE_IDS.last())
    }

    @Test
    fun `a known id is a tile`() {
        assertEquals(LocalAvatar.Tile("a1"), LocalAvatar.of("a1"))
        assertEquals("c10", (LocalAvatar.of("c10") as LocalAvatar.Tile).id)
    }

    /**
     * Every one of these was a storable value under the pre-profile scheme or
     * reads like one. None names bytes, so each must answer the monogram.
     */
    @Test
    fun `an id with nothing behind it is the monogram`() {
        listOf(null, "", "fallback", "default", "od-01", "a0", "a11", "d1", "A1").forEach { id ->
            assertEquals("resolving $id", LocalAvatar.Monogram, LocalAvatar.of(id))
        }
    }

    /** `fallback.webp` ships, but it was never a choice and must not become one. */
    @Test
    fun `the old fallback tile is not a choice`() {
        assertFalse(LocalAvatar.TILE_IDS.contains("fallback"))
    }

    @Test
    fun `only a tile has a stored id`() {
        assertNull(LocalAvatar.Monogram.storedId)
        assertEquals("b7", LocalAvatar.of("b7").storedId)
    }

    /** Storing and re-reading any choice must land on the same face. */
    @Test
    fun `every tile survives a store and read round trip`() {
        LocalAvatar.TILE_IDS.forEach { id ->
            val avatar = LocalAvatar.of(id)
            assertEquals(avatar, LocalAvatar.of(avatar.storedId))
        }
        assertEquals(LocalAvatar.Monogram, LocalAvatar.of(LocalAvatar.Monogram.storedId))
    }

    @Test
    fun `the monogram is a real choice rather than an absence`() {
        assertTrue(LocalAvatar.Monogram is LocalAvatar)
    }
}
