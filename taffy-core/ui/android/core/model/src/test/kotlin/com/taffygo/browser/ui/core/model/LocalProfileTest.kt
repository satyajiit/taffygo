// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** The name rules, which two screens share and neither may restate. */
class LocalProfileTest {

    @Test
    fun `an empty profile is complete`() {
        val profile = LocalProfile()
        assertNull(profile.displayName)
        assertEquals(LocalAvatar.Monogram, profile.avatar)
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, profile.monogram)
    }

    @Test
    fun `one word gives one letter and two give two`() {
        assertEquals("A", LocalProfile(displayName = "Ada").monogram)
        assertEquals("AL", LocalProfile(displayName = "Ada Lovelace").monogram)
    }

    /** Initials come from the ends, so a middle name does not displace a surname. */
    @Test
    fun `three words use the first and the last`() {
        assertEquals("AL", LocalProfile(displayName = "Ada King Lovelace").monogram)
        assertEquals("AL", LocalProfile(displayName = "Ada B C D Lovelace").monogram)
    }

    @Test
    fun `punctuation and spacing do not become initials`() {
        assertEquals("AL", LocalProfile(displayName = "  ada   lovelace  ").monogram)
        assertEquals("A", LocalProfile(displayName = "-Ada-").monogram)
        assertEquals(LocalProfile.MONOGRAM_FALLBACK, LocalProfile(displayName = "!!!").monogram)
    }

    /** A name may legitimately begin with a digit; refusing one is a silent demotion. */
    @Test
    fun `a digit is an initial`() {
        assertEquals("3", LocalProfile(displayName = "3rd").monogram)
    }

    @Test
    fun `blank input is no name at all`() {
        assertNull(LocalProfile.normalizedDisplayName(null))
        assertNull(LocalProfile.normalizedDisplayName(""))
        assertNull(LocalProfile.normalizedDisplayName("   \t \n "))
    }

    @Test
    fun `a name is trimmed but not otherwise rewritten`() {
        assertEquals("Ada  Lovelace", LocalProfile.normalizedDisplayName("  Ada  Lovelace  "))
    }

    @Test
    fun `an over-long name is bounded`() {
        val long = "x".repeat(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS + 25)
        val kept = LocalProfile.normalizedDisplayName(long)
        assertEquals(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS, kept?.length)
    }

    /**
     * The bound counts code points, not UTF-16 units. Counting `length` would
     * cut a name of astral characters at half its apparent size and could
     * split a surrogate pair, producing a stored name that is not text.
     */
    @Test
    fun `the bound counts characters a person would count`() {
        val emoji = "😀"
        val kept = LocalProfile.normalizedDisplayName(
            emoji.repeat(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS + 5),
        )
        assertEquals(
            LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS,
            kept?.codePointCount(0, kept.length),
        )
        assertEquals(emoji.repeat(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS), kept)
    }

    /**
     * The field's bound and the store's bound are the same number, and the
     * field's is the one a person can see working. A field that accepted
     * more would drop the tail on save with nothing having said so.
     */
    @Test
    fun `a field bound stops at the same length the store would keep`() {
        val long = "a".repeat(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS + 12)
        val bounded = LocalProfile.boundedDisplayName(long)
        assertEquals(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS, bounded.length)
        assertEquals(bounded, LocalProfile.normalizedDisplayName(long))
    }

    @Test
    fun `a field bound keeps the spaces a name is still being typed around`() {
        assertEquals("Ada ", LocalProfile.boundedDisplayName("Ada "))
        assertEquals("", LocalProfile.boundedDisplayName(""))
    }

    /** Code points, not chars: a bound counted in chars halves this name. */
    @Test
    fun `a field bound counts code points`() {
        val astral = "\uD83C\uDF3F".repeat(LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS + 4)
        val bounded = LocalProfile.boundedDisplayName(astral)
        assertEquals(
            LocalProfile.MAX_DISPLAY_NAME_CODE_POINTS,
            bounded.codePointCount(0, bounded.length),
        )
    }
}
