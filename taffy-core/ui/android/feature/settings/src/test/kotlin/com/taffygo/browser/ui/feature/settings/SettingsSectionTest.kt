// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.ui.TaffyDestination
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The section a destination opens, which is what the tablet list highlights.
 */
class SettingsSectionTest {

    @Test
    fun `every home row maps back from its destination`() {
        val home = SettingsSection.entries.filter { it.listedOnHome }
        assertEquals(
            listOf(
                SettingsSection.AD_AND_TRACKER_BLOCKING,
                SettingsSection.TAFFY,
                SettingsSection.PRIVACY,
                SettingsSection.APPEARANCE,
                SettingsSection.NOTIFICATIONS,
                SettingsSection.GENERAL,
                SettingsSection.PROFILES,
                SettingsSection.ABOUT_AND_HELP,
                SettingsSection.BACKUP,
            ),
            home,
        )
        for (section in home) {
            assertEquals(section, SettingsSection.of(section.destination))
        }
    }

    @Test
    fun `AI and providers highlights Taffy`() {
        assertEquals(
            SettingsSection.TAFFY,
            SettingsSection.of(TaffyDestination.AiAndProviders),
        )
        assertEquals(SettingsSection.TAFFY, SettingsSection.of(TaffyDestination.SkillsList))
        assertEquals(
            SettingsSection.TAFFY,
            SettingsSection.of(TaffyDestination.SkillDetail(skillId = "demo")),
        )
        assertEquals(SettingsSection.TAFFY, SettingsSection.of(TaffyDestination.Personality))
        assertEquals(
            SettingsSection.TAFFY,
            SettingsSection.of(TaffyDestination.PersonalityTuning),
        )
    }

    @Test
    fun `help and privacy children highlight their home row`() {
        assertEquals(
            SettingsSection.ABOUT_AND_HELP,
            SettingsSection.of(TaffyDestination.HelpAndFeedback),
        )
        assertEquals(SettingsSection.PRIVACY, SettingsSection.of(TaffyDestination.SiteSettings))
        assertEquals(
            SettingsSection.PRIVACY,
            SettingsSection.of(TaffyDestination.ClearBrowsingData),
        )
    }

    @Test
    fun `home groups cover every listed row once`() {
        val grouped = SettingsHomeGroup.entries.flatMap { it.members }
        assertEquals(grouped.size, grouped.toSet().size)
        assertEquals(
            SettingsSection.entries.filter { it.listedOnHome }.toSet(),
            grouped.toSet(),
        )
    }

    @Test
    fun `settings home is a list, not a section`() {
        assertNull(SettingsSection.of(TaffyDestination.SettingsHome))
    }

    @Test
    fun `every section has somewhere to go`() {
        val destinations = SettingsSection.entries.map { it.destination.route }
        assertEquals(destinations.size, destinations.toSet().size)
        assertTrue(destinations.all { it.isNotBlank() })
    }
}
