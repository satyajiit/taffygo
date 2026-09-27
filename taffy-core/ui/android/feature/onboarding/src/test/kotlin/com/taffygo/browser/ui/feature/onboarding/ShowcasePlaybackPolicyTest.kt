// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ShowcasePlaybackPolicyTest {
    @Test
    fun motionPlaybackDecodesWithOrWithoutSound() {
        assertTrue(
            shouldDecodeShowcase(
                playing = true,
                showFinalPoster = false,
                soundEnabled = false,
            ),
        )
    }

    @Test
    fun staticPosterDecodesOnlyWhenNarrationIsEnabled() {
        assertFalse(
            shouldDecodeShowcase(
                playing = true,
                showFinalPoster = true,
                soundEnabled = false,
            ),
        )
        assertTrue(
            shouldDecodeShowcase(
                playing = true,
                showFinalPoster = true,
                soundEnabled = true,
            ),
        )
    }

    @Test
    fun inactivePageNeverDecodes() {
        assertFalse(
            shouldDecodeShowcase(
                playing = false,
                showFinalPoster = false,
                soundEnabled = true,
            ),
        )
    }
}
