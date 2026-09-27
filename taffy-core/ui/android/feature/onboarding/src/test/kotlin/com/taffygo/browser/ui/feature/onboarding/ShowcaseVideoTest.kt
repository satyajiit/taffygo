// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.ui.unit.dp
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ShowcaseVideoTest {

    @Test
    fun `the intro has four videos`() {
        // Four rather than seven: `ShowcaseVideo` records which three stems are
        // held at OD-094 and why. If a permission is recorded and a stem comes
        // back, this number moves with it.
        assertEquals(4, ShowcaseVideo.entries.size)
    }

    @Test
    fun `no shipped film is one of the three held at OD-094`() {
        // The rights blocker is per stem, so the check that matters is by name.
        // A resource id cannot say which film it is; a stem name can.
        val held = setOf("COMPARE", "DRIVING_LICENCE", "MAIL")
        assertTrue(ShowcaseVideo.entries.none { it.name in held })
    }

    @Test
    fun `each video has distinct compile checked theme resources`() {
        val filmResources = ShowcaseVideo.entries.flatMap { video ->
            listOf(
                video.videoResourceId(isDark = false),
                video.videoResourceId(isDark = true),
                video.posterResourceId(isDark = false, finalFrame = true),
                video.posterResourceId(isDark = true, finalFrame = true),
            )
        }
        val startResources = ShowcaseVideo.entries.flatMap { video ->
            listOf(
                video.posterResourceId(isDark = false, finalFrame = false),
                video.posterResourceId(isDark = true, finalFrame = false),
            )
        }

        assertEquals(filmResources.size, filmResources.toSet().size)
        assertEquals(2, startResources.toSet().size)
        assertTrue((filmResources + startResources).all { resourceId -> resourceId != 0 })
    }

    @Test
    fun `auto advance leaves enough time for the longest video`() {
        assertTrue(SHOWCASE_CAROUSEL_DWELL_MS >= MAX_SHOWCASE_VIDEO_DURATION_MS)
    }

    @Test
    fun `low height and large text use the side by side layout`() {
        assertTrue(usesCompactShowcaseLayout(320.dp, fontScale = 1f))
        assertTrue(usesCompactShowcaseLayout(480.dp, fontScale = 2f))
        assertFalse(usesCompactShowcaseLayout(600.dp, fontScale = 2f))
        assertFalse(usesCompactShowcaseLayout(480.dp, fontScale = 1f))
    }
}
