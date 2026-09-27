// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LanguageRegionPolicyTest {

    @Test
    fun `English Hindi and system are offered in every country`() {
        listOf("IN", "in", "US", "JP", "", "IND", " IN ").forEach { regionCode ->
            assertEquals(
                AppLanguage.entries,
                LanguageRegionPolicy.availableLanguages(regionCode),
            )
            assertEquals(
                listOf("en", "hi"),
                LanguageRegionPolicy.availableLanguageTags(regionCode),
            )
            AppLanguage.entries.forEach { language ->
                assertTrue(LanguageRegionPolicy.isAvailable(language, regionCode))
            }
        }
    }

    @Test
    fun `changing country does not coerce a still-valid language`() {
        assertEquals(
            AppLanguage.HINDI,
            LanguageRegionPolicy.coerce(AppLanguage.HINDI, "US"),
        )
        assertEquals(
            AppLanguage.SYSTEM,
            LanguageRegionPolicy.coerce(AppLanguage.SYSTEM, "JP"),
        )
        assertEquals(
            AppLanguage.ENGLISH,
            LanguageRegionPolicy.coerce(AppLanguage.ENGLISH, "IN"),
        )
        assertEquals(
            AppLanguage.HINDI,
            LanguageRegionPolicy.coerce(AppLanguage.HINDI, "IN"),
        )
    }

    @Test
    fun `system follows the device catalogue and otherwise English`() {
        assertEquals(
            AppLanguage.HINDI,
            LanguageRegionPolicy.resolvedLanguage(AppLanguage.SYSTEM, "hi-IN"),
        )
        assertEquals(
            AppLanguage.HINDI,
            LanguageRegionPolicy.resolvedLanguage(AppLanguage.SYSTEM, "HI"),
        )
        assertEquals(
            AppLanguage.ENGLISH,
            LanguageRegionPolicy.resolvedLanguage(AppLanguage.SYSTEM, "en-US"),
        )
        assertEquals(
            AppLanguage.ENGLISH,
            LanguageRegionPolicy.resolvedLanguage(AppLanguage.SYSTEM, "ja-JP"),
        )
        assertEquals(
            AppLanguage.ENGLISH,
            LanguageRegionPolicy.resolvedLanguage(AppLanguage.SYSTEM, ""),
        )
    }

    @Test
    fun `an explicit language is not rewritten by the device tag`() {
        assertEquals(
            AppLanguage.HINDI,
            LanguageRegionPolicy.resolvedLanguage(AppLanguage.HINDI, "en-US"),
        )
        assertEquals(
            AppLanguage.ENGLISH,
            LanguageRegionPolicy.resolvedLanguage(AppLanguage.ENGLISH, "hi-IN"),
        )
    }

    @Test
    fun `region normalization accepts ISO codes only`() {
        assertEquals("IN", LanguageRegionPolicy.normalizedRegionCode("in"))
        assertEquals("US", LanguageRegionPolicy.normalizedRegionCode("US"))
        assertEquals(null, LanguageRegionPolicy.normalizedRegionCode("USA"))
        assertEquals(null, LanguageRegionPolicy.normalizedRegionCode(" IN "))
    }

    @Test
    fun `a country name is localized and a malformed code stays visible`() {
        assertEquals(
            "India",
            LanguageRegionPolicy.regionDisplayName("IN", Locale.ENGLISH),
        )
        assertEquals(
            "United States",
            LanguageRegionPolicy.regionDisplayName("us", Locale.ENGLISH),
        )
        assertEquals(
            "USA",
            LanguageRegionPolicy.regionDisplayName("USA", Locale.ENGLISH),
        )
    }
}
