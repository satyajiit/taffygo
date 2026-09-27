// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySegmentedControl
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-203: one bounded, profile-isolated download organizer. */
@Composable
fun DownloadsScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: DownloadsViewModel = screenViewModel(TaffyDestination.Downloads)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    DownloadsContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = { navigator.goBack() },
        modifier = modifier,
    )
}

/** Stateless render surface used by previews and accessibility tests. */
@Composable
fun DownloadsContent(
    state: DownloadsUiState,
    onIntent: (DownloadsIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    var pending by remember { mutableStateOf<PendingDownloadAction?>(null) }
    val pendingStillCurrent = pending?.let { requested ->
        state.groups.any { group ->
            group.downloads.any { it.id == requested.download.id && requested.action in it.allowedActions }
        }
    } ?: true
    LaunchedEffect(pending?.download?.id, pendingStillCurrent) {
        if (!pendingStillCurrent) pending = null
    }

    TaffyScreen(
        destination = TaffyDestination.Downloads,
        title = taffyString(R.string.taffy_downloads_title),
        onBack = onBack,
        modifier = modifier,
        scrollable = false,
    ) {
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .testTag(DOWNLOADS_LIST_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            item(key = "download-tabs", contentType = "controls") {
                val description = taffyString(R.string.taffy_downloads_tabs_description)
                TaffySegmentedControl(
                    options = listOf(
                        taffyString(R.string.taffy_downloads_tab_yours),
                        taffyString(R.string.taffy_downloads_tab_taffys),
                    ),
                    selectedIndex = DownloadsTab.entries.indexOf(state.tab),
                    onSelect = { onIntent(DownloadsIntent.SelectTab(DownloadsTab.entries[it])) },
                    modifier = Modifier
                        .testTag(DOWNLOADS_TABS_TEST_TAG)
                        .semantics { contentDescription = description },
                )
            }
            if (state.tab == DownloadsTab.TAFFYS) {
                item(key = "taffy-parts", contentType = "parts") {
                    TaffyPartsList(state = state, onIntent = onIntent)
                }
            } else {
                item(key = "organizer-controls", contentType = "controls") {
                    DownloadOrganizerControls(state, onIntent)
                }
                state.actionNotice?.let {
                    item(key = "action-notice", contentType = "notice") {
                        ActionUnavailableNotice(onIntent)
                    }
                }
                item(key = "collection-status", contentType = "status") {
                    CollectionStatus(state)
                }
                when {
                    state.isLoading -> item(key = "loading", contentType = "empty") {
                        TaffyEmptyState(
                            title = taffyString(R.string.taffy_downloads_loading_title),
                            body = taffyString(R.string.taffy_downloads_loading_body),
                        )
                    }
                    state.isUnavailable && state.totalCount == 0 -> item(
                        key = "unavailable",
                        contentType = "empty",
                    ) {
                        TaffyEmptyState(
                            title = taffyString(R.string.taffy_downloads_unavailable_title),
                            body = taffyString(R.string.taffy_downloads_unavailable_body),
                        )
                    }
                    state.hasNoDownloads -> item(key = "empty", contentType = "empty") {
                        TaffyEmptyState(
                            title = taffyString(R.string.taffy_downloads_empty_title),
                            body = taffyString(R.string.taffy_downloads_empty_body),
                        )
                    }
                    state.hasNoMatches -> item(key = "no-matches", contentType = "empty") {
                        TaffyEmptyState(
                            title = taffyString(R.string.taffy_downloads_no_matches_title),
                            body = taffyString(R.string.taffy_downloads_no_matches_body),
                        )
                    }
                    else -> downloadGroups(state, onIntent, onAskConfirmation = { pending = it })
                }
            }
        }
    }

    pending?.let { requested ->
        DownloadActionConfirmation(
            pending = requested,
            onDismiss = { pending = null },
            onConfirm = {
                when (requested.action) {
                    DownloadAction.CANCEL -> onIntent(DownloadsIntent.Cancel(requested.download.id))
                    DownloadAction.REMOVE -> onIntent(DownloadsIntent.Remove(requested.download.id))
                    else -> Unit
                }
                pending = null
            },
        )
    }
}

@Composable
private fun CollectionStatus(state: DownloadsUiState) {
    val text = when (state.collectionStatus) {
        DownloadCollectionStatus.LOADING -> taffyString(R.string.taffy_downloads_status_loading)
        DownloadCollectionStatus.UNAVAILABLE ->
            taffyString(R.string.taffy_downloads_status_unavailable)
        DownloadCollectionStatus.LIMITED -> taffyPlural(
            R.plurals.taffy_downloads_status_limited,
            state.totalCount,
            state.totalCount,
        )
        DownloadCollectionStatus.COMPLETE -> taffyPlural(
            R.plurals.taffy_downloads_status_count,
            state.totalCount,
            state.visibleCount,
            state.totalCount,
        )
    }
    Text(
        text = text,
        style = TaffyTheme.typography.label,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier
            .testTag(DOWNLOAD_STATUS_TEST_TAG)
            .semantics { liveRegion = LiveRegionMode.Polite },
    )
}

@Composable
private fun ActionUnavailableNotice(onIntent: (DownloadsIntent) -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
        Text(
            text = taffyString(R.string.taffy_downloads_action_unavailable),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.caution,
            modifier = Modifier
                .testTag(DOWNLOAD_ACTION_NOTICE_TEST_TAG)
                .semantics { liveRegion = LiveRegionMode.Polite },
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_downloads_dismiss),
            onClick = { onIntent(DownloadsIntent.DismissActionNotice) },
        )
    }
}

const val DOWNLOADS_TABS_TEST_TAG: String = "downloads_tabs"
const val DOWNLOADS_LIST_TEST_TAG: String = "downloads_list"
const val DOWNLOAD_STATUS_TEST_TAG: String = "download_collection_status"
const val DOWNLOAD_ACTION_NOTICE_TEST_TAG: String = "download_action_notice"
