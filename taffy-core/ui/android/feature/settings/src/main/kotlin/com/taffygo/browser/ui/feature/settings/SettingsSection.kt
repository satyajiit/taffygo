// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.annotation.StringRes
import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyIcon

/**
 * The settings search index, with a home-list flag.
 *
 * A blank query shows [listedOnHome] rows only. Search still finds every
 * entry. [of] maps every screen in the providers area — the hub, SCR-419 and
 * the pages under them — to [TAFFY], so the tablet list highlights Taffy while
 * any of them is open.
 */
enum class SettingsSection(
    val destination: TaffyDestination,
    val listedOnHome: Boolean,
    @StringRes val titleRes: Int,
    @StringRes val summaryRes: Int,
    private val iconProvider: () -> ImageVector,
) {
    AD_AND_TRACKER_BLOCKING(
        TaffyDestination.AdAndTrackerBlocking,
        true,
        R.string.taffy_settings_blocking_title,
        R.string.taffy_settings_blocking_summary,
        { TaffyIcon.ShieldCheck },
    ),
    TAFFY(
        TaffyDestination.TaffySettings,
        true,
        R.string.taffy_settings_taffy_title,
        R.string.taffy_settings_taffy_summary,
        { TaffyIcon.Sparkle },
    ),
    PRIVACY(
        TaffyDestination.Privacy,
        true,
        R.string.taffy_settings_privacy_title,
        R.string.taffy_settings_privacy_summary,
        { TaffyIcon.LockSimple },
    ),
    APPEARANCE(
        TaffyDestination.Appearance,
        true,
        R.string.taffy_settings_appearance_title,
        R.string.taffy_settings_appearance_summary,
        { TaffyIcon.CircleHalf },
    ),
    NOTIFICATIONS(
        TaffyDestination.Notifications,
        true,
        R.string.taffy_settings_notifications_title,
        R.string.taffy_settings_notifications_summary,
        { TaffyIcon.BellRinging },
    ),
    GENERAL(
        TaffyDestination.General,
        true,
        R.string.taffy_settings_general_title,
        R.string.taffy_settings_general_summary,
        { TaffyIcon.SlidersHorizontal },
    ),
    PROFILES(
        TaffyDestination.BrowserProfiles,
        true,
        R.string.taffy_settings_profiles_title,
        R.string.taffy_settings_profiles_summary,
        { TaffyIcon.UsersThree },
    ),
    ABOUT_AND_HELP(
        TaffyDestination.About,
        true,
        R.string.taffy_settings_about_title,
        R.string.taffy_settings_about_summary,
        { TaffyIcon.Info },
    ),
    BACKUP(
        TaffyDestination.Backup,
        true,
        R.string.taffy_settings_backup_title,
        R.string.taffy_settings_backup_summary,
        { TaffyIcon.LockSimple },
    ),
    YOU(
        TaffyDestination.You,
        false,
        R.string.taffy_settings_you_title,
        R.string.taffy_settings_you_summary,
        { TaffyIcon.UserCircle },
    ),
    TIME_ON_SITES(
        TaffyDestination.TimeOnSites,
        false,
        R.string.taffy_settings_time_on_sites_title,
        R.string.taffy_settings_time_on_sites_summary,
        { TaffyIcon.Clock },
    ),
    MEMORY(
        TaffyDestination.Memory,
        false,
        R.string.taffy_settings_memory_title,
        R.string.taffy_settings_memory_summary,
        { TaffyIcon.Sparkle },
    ),
    SAVED_SIGN_INS(
        TaffyDestination.SavedSignIns,
        false,
        R.string.taffy_settings_saved_sign_ins_title,
        R.string.taffy_settings_saved_sign_ins_summary,
        { TaffyIcon.Password },
    ),
    SAVED_DETAILS(
        TaffyDestination.SavedDetails,
        false,
        R.string.taffy_settings_saved_details_title,
        R.string.taffy_settings_saved_details_summary,
        { TaffyIcon.IdentificationCard },
    ),
    WHAT_HAPPENED(
        TaffyDestination.WhatHappened,
        false,
        R.string.taffy_settings_what_happened_title,
        R.string.taffy_settings_what_happened_summary,
        { TaffyIcon.Newspaper },
    ),
    LIBRARY(
        TaffyDestination.LibraryHome,
        false,
        R.string.taffy_settings_library_title,
        R.string.taffy_settings_library_summary,
        { TaffyIcon.Books },
    ),
    PERSONALITY(
        TaffyDestination.Personality,
        false,
        R.string.taffy_settings_personality_title,
        R.string.taffy_settings_personality_summary,
        { TaffyIcon.ChatCircle },
    ),
    WHAT_TAFFY_CAN_DO(
        TaffyDestination.SkillsList,
        false,
        R.string.taffy_settings_what_taffy_can_do_title,
        R.string.taffy_settings_what_taffy_can_do_summary,
        { TaffyIcon.PuzzlePiece },
    ),
    AI_AND_PROVIDERS(
        // The searchable entry to the providers area, which is SCR-419 for the
        // reason the Taffy settings row gives: it is where what a person has
        // is managed, and it forwards to the hub when they have nothing.
        TaffyDestination.ConnectedProviders,
        false,
        R.string.taffy_settings_ai_title,
        R.string.taffy_settings_ai_summary,
        { TaffyIcon.Key },
    ),
    ;

    val icon: ImageVector
        get() = iconProvider()

    companion object {
        /**
         * The home row [destination] belongs to, or null when it is not a
         * section. Nested Taffy, About, and Privacy screens highlight the
         * parent row on the tablet list.
         */
        fun of(destination: TaffyDestination): SettingsSection? = when (destination) {
            TaffyDestination.AiAndProviders,
            TaffyDestination.SkillsList,
            is TaffyDestination.SkillDetail,
            TaffyDestination.Personality,
            TaffyDestination.PersonalityTuning,
            is TaffyDestination.ProviderConfig,
            is TaffyDestination.ProviderSignIn,
            is TaffyDestination.ModelSelection,
            is TaffyDestination.CustomEndpointSetup,
            TaffyDestination.ConnectedProviders,
            -> TAFFY
            TaffyDestination.HelpAndFeedback -> ABOUT_AND_HELP
            TaffyDestination.SiteSettings,
            TaffyDestination.ClearBrowsingData,
            -> PRIVACY
            else -> entries.firstOrNull { it.destination == destination }
        }
    }
}

/**
 * Labeled cards on a blank query. Search results skip the eyebrows and sit
 * in one card.
 */
internal enum class SettingsHomeGroup(
    @StringRes val titleRes: Int,
    val members: List<SettingsSection>,
) {
    TAFFY(
        R.string.taffy_settings_taffy_title,
        listOf(SettingsSection.TAFFY),
    ),
    THIS_PHONE(
        R.string.taffy_settings_group_phone,
        listOf(
            SettingsSection.AD_AND_TRACKER_BLOCKING,
            SettingsSection.PRIVACY,
            SettingsSection.APPEARANCE,
            SettingsSection.NOTIFICATIONS,
            SettingsSection.GENERAL,
            SettingsSection.PROFILES,
            SettingsSection.BACKUP,
        ),
    ),
    ABOUT(
        R.string.taffy_settings_about_title,
        listOf(SettingsSection.ABOUT_AND_HELP),
    ),
}
