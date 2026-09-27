// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.ThemePreference
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AppearanceReducerTest {

    @Test
    fun `choosing a theme writes only the theme`() {
        val before = AppearanceUiState(theme = ThemePreference.SYSTEM)
        val after = reduceAppearance(before, AppearanceIntent.ChooseTheme(ThemePreference.DARK))
        assertEquals(ThemePreference.DARK, after.theme)
        assertEquals(before.appLanguage, after.appLanguage)
        assertEquals(before.regionCode, after.regionCode)
    }

    @Test
    fun `choosing a language writes only the language`() {
        val before = AppearanceUiState(regionCode = "US", appLanguage = AppLanguage.ENGLISH)
        val after = reduceAppearance(before, AppearanceIntent.ChooseLanguage(AppLanguage.HINDI))
        assertEquals(AppLanguage.HINDI, after.appLanguage)
        assertEquals("US", after.regionCode)
    }

    @Test
    fun `system and hindi can be chosen outside India`() {
        val before = AppearanceUiState(regionCode = "JP", appLanguage = AppLanguage.ENGLISH)
        val system = reduceAppearance(before, AppearanceIntent.ChooseLanguage(AppLanguage.SYSTEM))
        assertEquals(AppLanguage.SYSTEM, system.appLanguage)
        assertEquals("JP", system.regionCode)
        val hindi = reduceAppearance(before, AppearanceIntent.ChooseLanguage(AppLanguage.HINDI))
        assertEquals(AppLanguage.HINDI, hindi.appLanguage)
        assertEquals("JP", hindi.regionCode)
    }

    @Test
    fun `opening and closing the country sheet is local`() {
        val before = AppearanceUiState()
        val open = reduceAppearance(before, AppearanceIntent.OpenRegionPicker)
        assertTrue(open.regionPickerVisible)
        val typed = reduceAppearance(open, AppearanceIntent.RegionSearchChanged("jap"))
        assertEquals("jap", typed.regionSearchQuery)
        val closed = reduceAppearance(typed, AppearanceIntent.CloseRegionPicker)
        assertFalse(closed.regionPickerVisible)
        assertEquals("", closed.regionSearchQuery)
        assertEquals(before.regionCode, closed.regionCode)
        assertEquals(before.appLanguage, closed.appLanguage)
    }

    @Test
    fun `choosing a country keeps a still-valid language`() {
        val before = AppearanceUiState(
            regionCode = "IN",
            appLanguage = AppLanguage.HINDI,
            regionPickerVisible = true,
            regionSearchQuery = "united",
        )
        val after = reduceAppearance(before, AppearanceIntent.ChooseRegion("US"))
        assertEquals("US", after.regionCode)
        assertEquals(AppLanguage.HINDI, after.appLanguage)
        assertFalse(after.regionPickerVisible)
        assertEquals("", after.regionSearchQuery)
    }

    @Test
    fun `system language survives a country change`() {
        val before = AppearanceUiState(
            regionCode = "IN",
            appLanguage = AppLanguage.SYSTEM,
        )
        val after = reduceAppearance(before, AppearanceIntent.ChooseRegion("jp"))
        assertEquals("JP", after.regionCode)
        assertEquals(AppLanguage.SYSTEM, after.appLanguage)
    }

    @Test
    fun `a malformed country code is ignored`() {
        val before = AppearanceUiState(regionCode = "IN", regionPickerVisible = true)
        assertEquals(before, reduceAppearance(before, AppearanceIntent.ChooseRegion("USA")))
    }

    @Test
    fun `toggling pseudo localization flips only that switch`() {
        val before = AppearanceUiState()
        val after = reduceAppearance(before, AppearanceIntent.TogglePseudoLocalization)
        assertTrue(after.pseudoLocalization)
        assertEquals(before.theme, after.theme)
        assertEquals(before.appLanguage, after.appLanguage)
    }

    @Test
    fun `selecting a tab is local`() {
        val before = AppearanceUiState()
        val after = reduceAppearance(before, AppearanceIntent.SelectTab(AppearanceTab.LANGUAGE))
        assertEquals(AppearanceTab.LANGUAGE, after.tab)
        assertEquals(before.theme, after.theme)
        assertEquals(before.appLanguage, after.appLanguage)
    }

    @Test
    fun `toggling dark sites flips only that switch`() {
        val before = AppearanceUiState()
        val after = reduceAppearance(before, AppearanceIntent.ToggleForceDarkWeb)
        assertTrue(after.forceDarkWeb)
        assertEquals(before.theme, after.theme)
        assertFalse(after.pseudoLocalization)
    }
}
