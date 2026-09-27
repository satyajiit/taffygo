// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffySectionBar
import com.taffygo.browser.ui.core.ui.TaffyTileWash
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Profile, Saved sign-ins, Time on sites — the same compact list as settings. */
@Composable
internal fun YouChapters(
    state: YouUiState,
    onOpen: (YouRow) -> Unit,
) {
    TaffyGroupedCard(
        modifier = Modifier.testTag(YOU_LIST_TEST_TAG),
    ) {
        YouChapter.entries.forEachIndexed { index, chapter ->
            val row = chapter.row
            val enabled = youRowEnabled(row, state)
            SettingsHomeRow(
                title = taffyString(youRowTitle(row)),
                summary = if (enabled) {
                    youRowSummary(row, state)
                } else {
                    taffyString(R.string.taffy_you_private_unavailable)
                },
                icon = youRowIcon(row),
                testTag = "$YOU_ROW_TEST_TAG_PREFIX${row.name}",
                selected = false,
                accentSelected = false,
                onClick = { if (enabled) onOpen(row) },
            )
            if (index < YouChapter.entries.lastIndex) {
                TaffyGroupedCardDivider()
            }
        }
    }
}

/** Destinations that stay reachable from You, without a chapter number. */
@Composable
internal fun YouMoreRows(
    state: YouUiState,
    onOpen: (YouRow) -> Unit,
) {
    TaffySectionBar(
        title = taffyString(R.string.taffy_you_more_title),
        wash = TaffyTileWash.Neutral,
    )
    TaffyGroupedCard {
        YouMore.forEachIndexed { index, row ->
            val enabled = youRowEnabled(row, state)
            val summary = if (enabled) {
                youRowSummary(row, state)
            } else {
                taffyString(R.string.taffy_you_private_unavailable)
            }
            SettingsHomeRow(
                title = taffyString(youRowTitle(row)),
                summary = summary,
                icon = youRowIcon(row),
                testTag = "$YOU_ROW_TEST_TAG_PREFIX${row.name}",
                selected = false,
                accentSelected = false,
                onClick = { if (enabled) onOpen(row) },
            )
            if (index < YouMore.lastIndex) {
                TaffyGroupedCardDivider()
            }
        }
    }
}

@Composable
internal fun YouAdsNote() {
    TaffyInfoTile {
        Text(
            text = taffyString(R.string.taffy_you_ads_note),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

@Composable
internal fun youRowSummary(row: YouRow, state: YouUiState): String = when (row) {
    YouRow.PROFILE -> youShownName(state)
        ?: taffyString(R.string.taffy_you_profile_summary)
    YouRow.TIME_ON_SITES -> when (state.timeAvailability) {
        YouSurfaceAvailability.UNAVAILABLE,
        YouSurfaceAvailability.LOADING,
        -> taffyString(R.string.taffy_you_time_summary_unavailable)
        YouSurfaceAvailability.READY ->
            if (state.timeHasSites) {
                taffyString(R.string.taffy_you_time_summary_week)
            } else {
                taffyString(R.string.taffy_you_time_summary_empty)
            }
    }
    YouRow.MEMORY -> when (state.memoryAvailability) {
        YouSurfaceAvailability.UNAVAILABLE,
        YouSurfaceAvailability.LOADING,
        -> taffyString(R.string.taffy_you_memory_unavailable_summary)
        YouSurfaceAvailability.READY ->
            if (state.memoryCount == 0) {
                taffyString(R.string.taffy_you_memory_empty_summary)
            } else {
                taffyPlural(R.plurals.taffy_you_memory_notes, state.memoryCount, state.memoryCount)
            }
    }
    YouRow.SAVED_SIGN_INS -> when {
        state.signInsAvailability != YouSurfaceAvailability.READY ->
            taffyString(R.string.taffy_you_sign_ins_unavailable_summary)
        state.signInsCount == 0 -> taffyString(R.string.taffy_you_sign_ins_empty_summary)
        else -> taffyString(R.string.taffy_settings_saved_sign_ins_summary)
    }
    YouRow.SAVED_DETAILS -> when (state.detailsAvailability) {
        YouSurfaceAvailability.UNAVAILABLE,
        YouSurfaceAvailability.LOADING,
        -> taffyString(R.string.taffy_you_details_unavailable_summary)
        YouSurfaceAvailability.READY ->
            if (state.detailsCount == 0) {
                taffyString(R.string.taffy_you_details_empty_summary)
            } else {
                taffyString(R.string.taffy_you_details_summary)
            }
    }
    YouRow.WHAT_HAPPENED -> taffyString(R.string.taffy_you_happened_summary)
    YouRow.LIBRARY -> taffyString(R.string.taffy_you_library_summary)
}

internal fun youRowTitle(row: YouRow): Int = when (row) {
    YouRow.PROFILE -> R.string.taffy_you_profile_title
    YouRow.TIME_ON_SITES -> R.string.taffy_settings_time_on_sites_title
    YouRow.MEMORY -> R.string.taffy_settings_memory_title
    YouRow.SAVED_SIGN_INS -> R.string.taffy_settings_saved_sign_ins_title
    YouRow.SAVED_DETAILS -> R.string.taffy_settings_saved_details_title
    YouRow.WHAT_HAPPENED -> R.string.taffy_settings_what_happened_title
    YouRow.LIBRARY -> R.string.taffy_settings_library_title
}

internal fun youRowIcon(row: YouRow): ImageVector = when (row) {
    YouRow.PROFILE -> TaffyIcon.UserCircle
    YouRow.TIME_ON_SITES -> TaffyIcon.Clock
    YouRow.MEMORY -> TaffyIcon.Sparkle
    YouRow.SAVED_SIGN_INS -> TaffyIcon.Password
    YouRow.SAVED_DETAILS -> TaffyIcon.IdentificationCard
    YouRow.WHAT_HAPPENED -> TaffyIcon.Newspaper
    YouRow.LIBRARY -> TaffyIcon.Books
}

internal fun youRowEnabled(row: YouRow, state: YouUiState): Boolean = when (row) {
    YouRow.MEMORY,
    YouRow.SAVED_SIGN_INS,
    YouRow.LIBRARY,
    -> !state.privateTab
    else -> true
}

private enum class YouChapter(val row: YouRow) {
    PROFILE(YouRow.PROFILE),
    SAVED_SIGN_INS(YouRow.SAVED_SIGN_INS),
    TIME_ON_SITES(YouRow.TIME_ON_SITES),
}

private val YouMore: List<YouRow> = listOf(
    YouRow.MEMORY,
    YouRow.SAVED_DETAILS,
    YouRow.WHAT_HAPPENED,
    YouRow.LIBRARY,
)

/** Tags screen SCR-410's semantics tests name. */
const val YOU_HEADER_TEST_TAG: String = "you_header"
const val YOU_LIST_TEST_TAG: String = "you_list"
const val YOU_ROW_TEST_TAG_PREFIX: String = "you_row_"
