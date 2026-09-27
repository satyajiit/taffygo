// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.app.LocaleManager
import android.content.Context
import android.content.res.Configuration
import android.os.LocaleList
import androidx.test.core.app.ApplicationProvider
import com.taffygo.browser.ui.core.model.AppLanguage
import java.util.Locale
import org.chromium.base.LocaleUtils
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config

/**
 * The language a stored [AppLanguage] actually produces, on both sides of the
 * API 33 seam.
 *
 * WHAT THIS SUITE IS FOR. The defect it was written against was not a wrong
 * language — it was no language: the choice was stored, read back, and applied
 * by nothing, so the interface stayed English however loudly the control said
 * हिन्दी. Every case below therefore asserts that something *happens*, and the
 * two `@Config(sdk = ...)` values are the point rather than thoroughness: the
 * mechanism is different on each side of the platform's own seam, and a change
 * that quietly collapsed the two branches into one would leave half the
 * supported API range silently back where it started.
 *
 * WHAT IT CANNOT PROVE. That a phone redraws. The evidence for that is a run on
 * a device, and it is API 36 — the branch this file exercises at `sdk = 32` has
 * no device in this project to run on.
 */
@RunWith(BaseRobolectricTestRunner::class)
@Config(manifest = Config.NONE)
class TaffyAppLanguageTest {

    private lateinit var originalLocale: Locale
    private lateinit var originalLocales: LocaleList

    private val context: Context
        get() = ApplicationProvider.getApplicationContext()

    /**
     * The process default locales are global, and two of the cases below move
     * them on purpose. Held and put back so this suite cannot decide what
     * language another test in the same binary runs in.
     */
    @Before
    fun rememberTheProcessLocales() {
        originalLocale = Locale.getDefault()
        originalLocales = LocaleList.getDefault()
    }

    @After
    fun restoreTheProcessLocales() {
        Locale.setDefault(originalLocale)
        LocaleList.setDefault(originalLocales)
    }

    @Test
    @Config(sdk = [32])
    fun belowThirtyThreeThePerAppLocaleIsTaffyGosToApply() {
        assertFalse(TaffyAppLanguage.platformOwnsPerAppLocale())
    }

    @Test
    @Config(sdk = [33])
    fun fromThirtyThreeUpThePerAppLocaleIsThePlatforms() {
        assertTrue(TaffyAppLanguage.platformOwnsPerAppLocale())
    }

    @Test
    @Config(sdk = [33])
    fun systemChoiceUnpinsThePerAppLocaleInEveryCountry() {
        val manager = context.getSystemService(LocaleManager::class.java)
        manager.applicationLocales = LocaleList.forLanguageTags("hi")
        try {
            TaffyAppLanguage.apply(context, AppLanguage.SYSTEM, "US")

            assertEquals(LocaleList.getEmptyLocaleList(), manager.applicationLocales)
        } finally {
            manager.applicationLocales = LocaleList.getEmptyLocaleList()
        }
    }

    @Test
    @Config(sdk = [33])
    fun hindiRemainsAvailableOutsideIndia() {
        val manager = context.getSystemService(LocaleManager::class.java)
        manager.applicationLocales = LocaleList.forLanguageTags("en")
        try {
            TaffyAppLanguage.apply(context, AppLanguage.HINDI, "US")

            assertEquals(LocaleList.forLanguageTags("hi"), manager.applicationLocales)
        } finally {
            manager.applicationLocales = LocaleList.getEmptyLocaleList()
        }
    }

    @Test
    @Config(sdk = [32])
    fun hindiReachesTheActivityConfigurationBelowThirtyThree() {
        val config = Configuration().apply { fontScale = 0f }

        assertTrue(TaffyAppLanguage.overrideConfiguration(context, config, AppLanguage.HINDI))

        assertEquals(
            "the activity's own resources have to resolve in the chosen language",
            "hi",
            LocaleUtils.toBaseLanguage(LocaleUtils.getConfigurationLanguage(config)),
        )
        assertEquals(
            "and so does everything that reads the process default — the native"
                + " browser process included",
            "hi",
            LocaleUtils.toBaseLanguage(LocaleList.getDefault().get(0).toLanguageTag()),
        )
    }

    @Test
    @Config(sdk = [32])
    fun theSystemChoiceOverridesNothingAndPutsTheProcessLocalesBack() {
        val config = Configuration().apply { fontScale = 0f }
        TaffyAppLanguage.overrideConfiguration(context, config, AppLanguage.HINDI)

        val system = Configuration().apply { fontScale = 0f }
        assertFalse(
            "following the device is the base context's own answer, so there is"
                + " nothing to override",
            TaffyAppLanguage.overrideConfiguration(context, system, AppLanguage.SYSTEM),
        )

        assertTrue(system.locales.isEmpty)
        assertEquals(
            "the activity being replaced moved the process locales, and choosing"
                + " the device has to move them back",
            LocaleUtils.getContextLanguage(context),
            LocaleList.getDefault().get(0).toLanguageTag(),
        )
    }

    @Test
    @Config(sdk = [36])
    fun fromThirtyThreeUpTheActivityConfigurationIsLeftAlone() {
        val config = Configuration().apply { fontScale = 0f }

        assertFalse(
            "the platform has already applied the choice to the whole"
                + " application, and Chromium reads that same store",
            TaffyAppLanguage.overrideConfiguration(context, config, AppLanguage.HINDI),
        )
        assertTrue(config.locales.isEmpty)
    }

    @Test
    @Config(sdk = [32])
    fun theConfiguredLanguageIsWhatWasLastAppliedToAConfiguration() {
        TaffyAppLanguage.overrideConfiguration(context, Configuration(), AppLanguage.HINDI)
        assertEquals(AppLanguage.HINDI, TaffyAppLanguage.configuredLanguage())

        TaffyAppLanguage.overrideConfiguration(context, Configuration(), AppLanguage.ENGLISH)
        assertEquals(AppLanguage.ENGLISH, TaffyAppLanguage.configuredLanguage())
    }

    @Test
    fun anActivityIsRebuiltOnlyWhenItWasBuiltForAnotherLanguage() {
        assertTrue(TaffyAppLanguage.needsRecreate(AppLanguage.ENGLISH, AppLanguage.HINDI))
        assertFalse(TaffyAppLanguage.needsRecreate(AppLanguage.HINDI, AppLanguage.HINDI))
    }

    @Test
    fun anActivityNoOverrideWasAppliedToIsNeverRebuilt() {
        // The loop breaker. Rebuilding it would produce another activity in the
        // same language, which would ask the same question and get the same
        // answer, for as long as the phone had battery.
        assertFalse(TaffyAppLanguage.needsRecreate(null, AppLanguage.HINDI))
    }

    @Test
    fun theFrameworkIsAskedForTheChosenLanguageWhenTheAppRunsInAnotherOne() {
        val toSet = TaffyAppLanguage.localesToSet(
            LocaleList.forLanguageTags("en-US"),
            LocaleList.forLanguageTags("en-US"),
            AppLanguage.HINDI,
        )
        assertEquals(LocaleList.forLanguageTags("hi"), toSet)
    }

    @Test
    fun theFrameworkIsAskedForNothingWhenItAlreadyHoldsTheChosenLanguage() {
        assertNull(
            TaffyAppLanguage.localesToSet(
                LocaleList.forLanguageTags("hi"),
                LocaleList.forLanguageTags("en-US"),
                AppLanguage.HINDI,
            ),
        )
    }

    @Test
    fun aRegionTheFrameworkAddedIsNotTreatedAsADifferentChoice() {
        // The framework may normalise `hi` to `hi-IN`. Read as a different
        // answer, that would set the locales again on every launch — and
        // setting them is what makes the system restart the application, so the
        // cost of a strict comparison here is a restart loop rather than a
        // redundant call.
        assertNull(
            TaffyAppLanguage.localesToSet(
                LocaleList.forLanguageTags("hi-IN"),
                LocaleList.forLanguageTags("en-US"),
                AppLanguage.HINDI,
            ),
        )
    }

    @Test
    fun nothingIsPinnedWhenTheDeviceAlreadySpeaksTheChosenLanguage() {
        // The first launch after an install, on an English phone, with the
        // compiled-in default of English. Pinning here would restart the
        // application to reach the state it was already in — measured on a
        // phone as the task emptying and the launcher coming back.
        assertNull(
            TaffyAppLanguage.localesToSet(
                LocaleList.getEmptyLocaleList(),
                LocaleList.forLanguageTags("en-IN"),
                AppLanguage.ENGLISH,
            ),
        )
    }

    @Test
    fun thePinIsTakenWhenTheDeviceSpeaksSomethingElse() {
        // The same first launch on a Hindi phone. Here the default means
        // something, so the restart buys the language the setting claims.
        assertEquals(
            LocaleList.forLanguageTags("en"),
            TaffyAppLanguage.localesToSet(
                LocaleList.getEmptyLocaleList(),
                LocaleList.forLanguageTags("hi-IN"),
                AppLanguage.ENGLISH,
            ),
        )
    }

    @Test
    fun followingTheDeviceIsTheEmptyLocaleList() {
        assertEquals(
            LocaleList.getEmptyLocaleList(),
            TaffyAppLanguage.localesToSet(
                LocaleList.forLanguageTags("hi"),
                LocaleList.forLanguageTags("en-IN"),
                AppLanguage.SYSTEM,
            ),
        )
    }

    @Test
    fun followingTheDeviceIsAlreadyTrueWhenNothingIsPinned() {
        // And it stays true whatever the device is set to, which is why this
        // one case may not be answered from the device's own locales.
        assertNull(
            TaffyAppLanguage.localesToSet(
                LocaleList.getEmptyLocaleList(),
                LocaleList.forLanguageTags("hi-IN"),
                AppLanguage.SYSTEM,
            ),
        )
    }
}
