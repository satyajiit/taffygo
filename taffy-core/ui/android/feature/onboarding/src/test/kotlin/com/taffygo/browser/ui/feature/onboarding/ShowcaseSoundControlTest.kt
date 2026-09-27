// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import android.media.AudioManager
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ShowcaseSoundControlTest {
    @Test
    fun normalRingerStartsWithSound() {
        assertTrue(showcaseSoundEnabledForRingerMode(AudioManager.RINGER_MODE_NORMAL))
    }

    @Test
    fun silentAndVibrateRingersStartMuted() {
        assertFalse(showcaseSoundEnabledForRingerMode(AudioManager.RINGER_MODE_SILENT))
        assertFalse(showcaseSoundEnabledForRingerMode(AudioManager.RINGER_MODE_VIBRATE))
    }

    @Test
    fun unknownRingerModeStartsMuted() {
        assertFalse(showcaseSoundEnabledForRingerMode(Int.MIN_VALUE))
    }
}
