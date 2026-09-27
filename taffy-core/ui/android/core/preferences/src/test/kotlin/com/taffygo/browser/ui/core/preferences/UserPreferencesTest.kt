// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.preferences

import com.taffygo.browser.ui.core.model.AppLanguage
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Test

/**
 * The compiled-in defaults are what every screen renders before disk answers,
 * so a wrong default is a visible flash of the wrong choice.
 */
class UserPreferencesTest {

    @Test
    fun `compiled fallback is India following the system`() {
        assertEquals(DEFAULT_REGION_CODE, UserPreferences().regionCode)
        assertEquals(AppLanguage.SYSTEM, UserPreferences().appLanguage)
    }

    @Test
    fun `Hindi and system remain valid outside India`() {
        assertEquals(
            AppLanguage.HINDI,
            UserPreferences(regionCode = "US", appLanguage = AppLanguage.HINDI).appLanguage,
        )
        assertEquals(
            AppLanguage.SYSTEM,
            UserPreferences(regionCode = "JP", appLanguage = AppLanguage.SYSTEM).appLanguage,
        )
    }

    @Test
    fun `an invalid country cannot enter observable preferences`() {
        assertThrows(IllegalArgumentException::class.java) {
            UserPreferences(regionCode = "USA", appLanguage = AppLanguage.ENGLISH)
        }
    }

    @Test
    fun `a fresh install has not completed onboarding`() {
        assertFalse(UserPreferences().onboardingCompleted)
    }

    @Test
    fun `the composer offers nothing until the user turns it on`() {
        // A suggestion is a model call made from what somebody is halfway
        // through typing (decision 0097). The default is the whole of the
        // protection, so nobody may be opted into it by one.
        assertFalse(UserPreferences().composerSuggestions)
    }

    @Test
    fun `defaults are not mistaken for stored choices`() {
        assertFalse(UserPreferences().loaded)
    }
}
