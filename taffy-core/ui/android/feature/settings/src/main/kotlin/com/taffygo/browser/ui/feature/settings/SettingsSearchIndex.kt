// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/**
 * Settings search: home rows on a blank query, every indexed leaf otherwise.
 *
 * Synonyms live in string resources and are never shown as a row subtitle.
 * A query of `history` is a miss on purpose.
 */
object SettingsSearchIndex {

    /** Sections whose localized title, summary, or synonyms match [query]. */
    fun matching(
        query: String,
        sections: List<SettingsSection>,
        searchTextOf: (SettingsSection) -> Iterable<String>,
    ): List<SettingsSection> {
        val trimmed = query.trim()
        return if (trimmed.isEmpty()) {
            sections.filter { it.listedOnHome }
        } else {
            sections.filter { section ->
                searchTextOf(section).any { text -> text.contains(trimmed, ignoreCase = true) }
            }
        }
    }

    /** Display rows for [query], in index order. */
    fun hits(
        query: String,
        sections: List<SettingsSection>,
        searchTextOf: (SettingsSection) -> Iterable<String>,
        titleOf: (SettingsSection) -> String,
        summaryOf: (SettingsSection) -> String,
    ): List<SettingsSearchHit> = matching(query, sections, searchTextOf).map { section ->
        SettingsSearchHit(
            destination = section.destination,
            title = titleOf(section),
            summary = summaryOf(section),
            section = section,
        )
    }

    /** Extra search-only words for [section], or null when title and summary suffice. */
    fun synonymRes(section: SettingsSection): Int? = when (section) {
        SettingsSection.YOU -> R.string.taffy_settings_you_search
        SettingsSection.MEMORY -> R.string.taffy_settings_memory_search
        SettingsSection.SAVED_SIGN_INS -> R.string.taffy_settings_saved_sign_ins_search
        SettingsSection.SAVED_DETAILS -> R.string.taffy_settings_saved_details_search
        SettingsSection.WHAT_TAFFY_CAN_DO -> R.string.taffy_settings_skills_search
        SettingsSection.TAFFY -> R.string.taffy_settings_taffy_search
        SettingsSection.LIBRARY -> R.string.taffy_settings_library_search
        SettingsSection.TIME_ON_SITES -> R.string.taffy_settings_time_on_sites_search
        SettingsSection.WHAT_HAPPENED -> R.string.taffy_settings_what_happened_search
        SettingsSection.AD_AND_TRACKER_BLOCKING -> R.string.taffy_settings_blocking_search
        SettingsSection.AI_AND_PROVIDERS -> R.string.taffy_settings_ai_search
        SettingsSection.PERSONALITY -> R.string.taffy_settings_personality_search
        SettingsSection.BACKUP -> R.string.taffy_settings_backup_search
        SettingsSection.PRIVACY,
        SettingsSection.APPEARANCE,
        SettingsSection.NOTIFICATIONS,
        SettingsSection.GENERAL,
        SettingsSection.PROFILES,
        SettingsSection.ABOUT_AND_HELP,
        -> null
    }
}
