// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import android.text.format.DateUtils
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.TaffyStatusChip
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyGroupedCardItems
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-304 — the workspace list.
 *
 * Each row shows the state the workspace really reached as a word, a shape, and
 * a tone. A partly-done workspace stays partly done here for as long as it is
 * partly done. Amber sits only on a row Taffy is still working.
 *
 * The list names the browser profile it belongs to when the device has more
 * than one, and offers a new workspace through the Ask sheet with the
 * source-table shape stated, so a workspace can be started on purpose rather than only as a by-product of a
 * task (decision 0102).
 */
@Composable
fun WorkspaceListScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    selectedWorkspaceId: String? = null,
    onBack: () -> Unit = { navigator.goBack() },
) {
    val viewModel: WorkspaceListViewModel = screenViewModel(TaffyDestination.WorkspaceList)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    WorkspaceListContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = onBack,
        selectedWorkspaceId = selectedWorkspaceId,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun WorkspaceListContent(
    state: WorkspaceListUiState,
    onIntent: (WorkspaceListIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    selectedWorkspaceId: String? = null,
) {
    val showWorkspaces = !state.loading && !state.unavailable &&
        !state.isEmpty && !state.hasNoMatches
    TaffyLazyScreen(
        destination = TaffyDestination.WorkspaceList,
        title = taffyString(R.string.taffy_workspace_list_title),
        onBack = onBack,
        modifier = modifier,
        subtitle = state.profileName?.let {
            taffyString(R.string.taffy_workspace_list_profile, it)
        },
        listModifier = if (showWorkspaces) Modifier.testTag(LIST_TEST_TAG) else Modifier,
    ) {
        item(key = "workspace-search", contentType = "search") {
            TaffySearchField(
                value = state.query,
                onValueChange = { onIntent(WorkspaceListIntent.QueryChanged(it)) },
                placeholder = taffyString(R.string.taffy_workspace_list_search),
                testTag = SEARCH_TEST_TAG,
            )
        }
        item(key = "workspace-create", contentType = "action") {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_workspace_list_create),
                onClick = { onIntent(WorkspaceListIntent.Create) },
                icon = TaffyIcon.Plus,
                testTag = CREATE_TEST_TAG,
            )
        }

        when {
            state.loading -> item(key = "workspace-loading", contentType = "status") {
                WorkspaceSkeletonList(
                    loadingDescription = taffyString(R.string.taffy_workspace_list_loading),
                    testTag = LIST_LOADING_TEST_TAG,
                )
            }
            state.unavailable -> item(key = "workspace-unavailable", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_workspace_unavailable_title),
                    body = taffyString(R.string.taffy_workspace_unavailable_body),
                    leading = { WorkspaceEmptyGlyph() },
                )
            }
            state.isEmpty -> item(key = "workspace-empty", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_workspace_list_empty_title),
                    body = taffyString(R.string.taffy_workspace_list_empty_body),
                    leading = { WorkspaceEmptyGlyph() },
                )
            }
            state.hasNoMatches -> item(key = "workspace-no-matches", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_workspace_list_no_matches_title),
                    body = taffyString(R.string.taffy_workspace_list_no_matches_body),
                    leading = { WorkspaceEmptyGlyph(TaffyIcon.MagnifyingGlass) },
                )
            }
            else -> taffyGroupedCardItems(
                values = state.workspaces,
                key = { it.id.value },
                contentType = { "workspace" },
            ) { workspace ->
                val presentation = StatusPresentation.of(workspace.state)
                val stateWord = taffyString(presentation.labelRes)
                val sources = taffyPlural(
                    R.plurals.taffy_workspace_list_sources,
                    workspace.activeSourceCount,
                    workspace.activeSourceCount,
                )
                val conflicts = taffyPlural(
                    R.plurals.taffy_workspace_list_conflicts,
                    workspace.conflictCount,
                    workspace.conflictCount,
                )
                val freshness = DateUtils
                    .getRelativeTimeSpanString(workspace.lastUpdatedEpochMillis)
                    .toString()
                val taffyWorking = workspace.state == TaskDisplayState.RUNNING ||
                    workspace.state == TaskDisplayState.WAITING_FOR_YOU
                WorkspaceRecordRow(
                    title = workspace.displayName,
                    supporting = taffyString(
                        R.string.taffy_workspace_list_supporting,
                        sources,
                        conflicts,
                        freshness,
                    ),
                    accessibleDescription = taffyString(
                        R.string.taffy_workspace_list_description,
                        workspace.displayName,
                        stateWord,
                        sources,
                    ),
                    glyph = TaffyIcon.SquaresFour,
                    testTag = "$WORKSPACE_TEST_TAG_PREFIX${workspace.id.value}",
                    selected = workspace.id.value == selectedWorkspaceId,
                    glyphTaffyInk = taffyWorking,
                    onClick = { onIntent(WorkspaceListIntent.Open(workspace.id)) },
                    trailing = { TaffyStatusChip(presentation = presentation) },
                )
            }
        }
    }
}

/** The tags screen SCR-304's semantics tests name. */
const val SEARCH_TEST_TAG: String = "workspace_list_search"
const val CREATE_TEST_TAG: String = "workspace_list_create"
const val LIST_TEST_TAG: String = "workspace_list"
const val LIST_LOADING_TEST_TAG: String = "workspace_list_loading"
const val WORKSPACE_TEST_TAG_PREFIX: String = "workspace_"
