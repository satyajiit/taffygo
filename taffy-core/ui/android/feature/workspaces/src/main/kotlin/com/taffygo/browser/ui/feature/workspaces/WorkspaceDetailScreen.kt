// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffyStatusChip
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-305 — one workspace.
 *
 * Every fact shows its kind and its source chip, and a fact whose only source
 * was excluded says it needs a new one rather than quietly keeping the value.
 * Amber is reserved for a conflict-touched value, not the whole table.
 */
@Composable
fun WorkspaceDetailScreen(
    destination: TaffyDestination.WorkspaceDetail,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: WorkspaceDetailViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    WorkspaceDetailContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun WorkspaceDetailContent(
    state: WorkspaceDetailUiState,
    onIntent: (WorkspaceDetailIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    var showRename by remember(state.id, state.revision) { mutableStateOf(false) }
    var showDelete by remember(state.id, state.revision) { mutableStateOf(false) }
    val title = when {
        state.loading || state.unavailable -> taffyString(R.string.taffy_workspace_list_title)
        state.displayName.isNotEmpty() -> state.displayName
        else -> taffyString(R.string.taffy_workspace_detail_missing_title)
    }
    val sourcesById = remember(state.sources) { state.sources.associateBy { it.id } }
    TaffyLazyScreen(
        destination = TaffyDestination.WorkspaceDetail(state.id?.value.orEmpty()),
        title = title,
        onBack = onBack,
        modifier = modifier,
        listModifier = Modifier.testTag(DETAIL_LIST_TEST_TAG),
    ) {
        when {
            state.loading -> item(key = "detail-loading", contentType = "status") {
                WorkspaceSkeletonList(
                    loadingDescription = taffyString(R.string.taffy_workspace_detail_loading),
                    testTag = DETAIL_LOADING_TEST_TAG,
                )
            }
            state.unavailable -> item(key = "detail-unavailable", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_workspace_unavailable_title),
                    body = taffyString(R.string.taffy_workspace_unavailable_body),
                    leading = { WorkspaceEmptyGlyph() },
                )
            }
            state.missing -> item(key = "detail-missing", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_workspace_detail_missing_title),
                    body = taffyString(R.string.taffy_workspace_detail_missing_body),
                    leading = { WorkspaceEmptyGlyph() },
                )
            }
            else -> {
                item(key = "detail-status", contentType = "status") {
                    TaffyStatusChip(presentation = StatusPresentation.of(state.state))
                }
                if (state.conflictCount > 0 || state.needsANewSourceCount > 0) {
                    item(key = "detail-notices", contentType = "notices") {
                        WorkspaceDetailNotices(state)
                    }
                }
                item(key = "detail-export", contentType = "action") {
                    TaffyPrimaryButton(
                        label = taffyString(R.string.taffy_workspace_detail_export),
                        onClick = { onIntent(WorkspaceDetailIntent.Export) },
                        testTag = DETAIL_EXPORT_TEST_TAG,
                        icon = TaffyIcon.Export,
                    )
                }
                item(key = "detail-rename", contentType = "action") {
                    TaffySecondaryButton(
                        label = taffyString(R.string.taffy_workspace_rename_action),
                        onClick = { showRename = true },
                        testTag = DETAIL_RENAME_TEST_TAG,
                    )
                }
                item(key = "detail-delete", contentType = "action") {
                    TaffyDangerButton(
                        label = taffyString(R.string.taffy_workspace_delete_action),
                        onClick = { showDelete = true },
                        enabled = state.deletionPreview != null,
                        testTag = DETAIL_DELETE_TEST_TAG,
                    )
                }
                workspaceDetailFacts(
                    state = state,
                    sourcesById = sourcesById,
                    onIntent = onIntent,
                )
                workspaceDetailSources(state = state, onIntent = onIntent)
            }
        }
    }
    if (showRename) {
        WorkspaceRenameDialog(
            currentName = state.displayName,
            onDismiss = { showRename = false },
            onConfirm = { name ->
                onIntent(WorkspaceDetailIntent.Rename(state.revision, name))
                showRename = false
            },
        )
    }
    val preview = state.deletionPreview
    if (showDelete && preview != null) {
        WorkspaceDeleteDialog(
            preview = preview,
            onDismiss = { showDelete = false },
            onConfirm = {
                onIntent(
                    WorkspaceDetailIntent.Delete(
                        expectedRevision = state.revision,
                        confirmationToken = preview.confirmationToken,
                    ),
                )
                showDelete = false
            },
        )
    }
}

/** The tags screen SCR-305's semantics tests name. */
const val CONFLICT_TEST_TAG: String = "workspace_detail_conflicts"
const val NEEDS_SOURCE_TEST_TAG: String = "workspace_detail_needs_sources"
const val DETAIL_EXPORT_TEST_TAG: String = "workspace_detail_export"
const val DETAIL_LIST_TEST_TAG: String = "workspace_detail_list"
const val DETAIL_OUTPUT_TEST_TAG: String = "workspace_detail_output"
const val DETAIL_FACT_TEST_TAG_PREFIX: String = "workspace_detail_fact_"
const val DETAIL_SOURCES_TEST_TAG: String = "workspace_detail_sources"
const val DETAIL_SOURCE_TEST_TAG_PREFIX: String = "workspace_detail_source_"
const val DETAIL_EXCLUDE_TEST_TAG_PREFIX: String = "workspace_detail_exclude_"
const val DETAIL_LOADING_TEST_TAG: String = "workspace_detail_loading"
const val DETAIL_RENAME_TEST_TAG: String = "workspace_detail_rename"
const val DETAIL_DELETE_TEST_TAG: String = "workspace_detail_delete"
