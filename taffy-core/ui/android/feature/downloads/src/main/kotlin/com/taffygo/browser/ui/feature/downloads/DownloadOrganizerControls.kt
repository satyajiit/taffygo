// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.TaffySegmentedControl
import com.taffygo.browser.ui.core.ui.taffyString

/** Search, filter, sort and grouping controls over an already bounded profile snapshot. */
@Composable
internal fun DownloadOrganizerControls(
    state: DownloadsUiState,
    onIntent: (DownloadsIntent) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        TaffySearchField(
            value = state.query,
            onValueChange = { onIntent(DownloadsIntent.SetQuery(it)) },
            placeholder = taffyString(R.string.taffy_downloads_search),
            clearContentDescription = taffyString(R.string.taffy_downloads_search_clear),
            testTag = DOWNLOAD_SEARCH_TEST_TAG,
        )
        OrganizerChoice(
            label = taffyString(R.string.taffy_downloads_filter_label),
            options = DownloadFilter.entries.map { filter -> filterLabel(filter) },
            selectedIndex = DownloadFilter.entries.indexOf(state.filter),
            onSelect = { onIntent(DownloadsIntent.SelectFilter(DownloadFilter.entries[it])) },
            testTag = DOWNLOAD_FILTER_TEST_TAG,
        )
        OrganizerChoice(
            label = taffyString(R.string.taffy_downloads_sort_label),
            options = DownloadSort.entries.map { sort -> sortLabel(sort) },
            selectedIndex = DownloadSort.entries.indexOf(state.sort),
            onSelect = { onIntent(DownloadsIntent.SelectSort(DownloadSort.entries[it])) },
            testTag = DOWNLOAD_SORT_TEST_TAG,
        )
        OrganizerChoice(
            label = taffyString(R.string.taffy_downloads_group_label),
            options = DownloadGrouping.entries.map { grouping -> groupingLabel(grouping) },
            selectedIndex = DownloadGrouping.entries.indexOf(state.grouping),
            onSelect = {
                onIntent(DownloadsIntent.SelectGrouping(DownloadGrouping.entries[it]))
            },
            testTag = DOWNLOAD_GROUPING_TEST_TAG,
        )
    }
}

@Composable
private fun OrganizerChoice(
    label: String,
    options: List<String>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    testTag: String,
) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
        Text(text = label, style = TaffyTheme.typography.label, color = TaffyTheme.colors.textSecondary)
        TaffySegmentedControl(
            options = options,
            selectedIndex = selectedIndex,
            onSelect = onSelect,
            modifier = Modifier.testTag(testTag),
            optionModifier = { index -> Modifier.testTag("${testTag}_$index") },
        )
    }
}

@Composable
private fun filterLabel(filter: DownloadFilter): String = taffyString(
    when (filter) {
        DownloadFilter.ALL -> R.string.taffy_downloads_filter_all
        DownloadFilter.ACTIVE -> R.string.taffy_downloads_filter_active
        DownloadFilter.COMPLETE -> R.string.taffy_downloads_filter_complete
        DownloadFilter.FAILED -> R.string.taffy_downloads_filter_failed
    },
)

@Composable
private fun sortLabel(sort: DownloadSort): String = taffyString(
    when (sort) {
        DownloadSort.NEWEST -> R.string.taffy_downloads_sort_newest
        DownloadSort.NAME -> R.string.taffy_downloads_sort_name
        DownloadSort.LARGEST -> R.string.taffy_downloads_sort_largest
        DownloadSort.SOURCE -> R.string.taffy_downloads_sort_source
    },
)

@Composable
private fun groupingLabel(grouping: DownloadGrouping): String = taffyString(
    when (grouping) {
        DownloadGrouping.NONE -> R.string.taffy_downloads_group_none
        DownloadGrouping.STATUS -> R.string.taffy_downloads_group_status
        DownloadGrouping.FILE_TYPE -> R.string.taffy_downloads_group_type
        DownloadGrouping.SOURCE -> R.string.taffy_downloads_group_source
    },
)

const val DOWNLOAD_SEARCH_TEST_TAG: String = "download_search"
const val DOWNLOAD_FILTER_TEST_TAG: String = "download_filter"
const val DOWNLOAD_SORT_TEST_TAG: String = "download_sort"
const val DOWNLOAD_GROUPING_TEST_TAG: String = "download_grouping"
