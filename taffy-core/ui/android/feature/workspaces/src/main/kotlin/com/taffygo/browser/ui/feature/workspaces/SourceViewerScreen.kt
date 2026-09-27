// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import android.text.format.DateUtils
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyKeyValueRow
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-306 — the source viewer.
 *
 * The UI layer has no page reader, so it shows what was recorded about the read
 * rather than a rendering of the page: the title, the host, when it was read,
 * and how many facts came from it.
 */
@Composable
fun SourceViewerScreen(
    destination: TaffyDestination.SourceViewer,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: SourceViewerViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    SourceViewerContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        destination = destination,
        showUp = showUp,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun SourceViewerContent(
    state: SourceViewerUiState,
    onIntent: (SourceViewerIntent) -> Unit,
    modifier: Modifier = Modifier,
    destination: TaffyDestination.SourceViewer = TaffyDestination.SourceViewer("", ""),
    showUp: Boolean = true,
) {
    val title = when {
        state.loading || state.unavailable -> taffyString(R.string.taffy_workspace_list_title)
        state.title.isNotEmpty() -> state.title
        else -> taffyString(R.string.taffy_source_viewer_missing_title)
    }
    TaffyScreen(
        destination = destination,
        title = title,
        onBack = if (showUp) ({ onIntent(SourceViewerIntent.Close) }) else null,
        modifier = modifier,
    ) {
        when {
            state.loading -> WorkspaceSkeletonList(
                loadingDescription = taffyString(R.string.taffy_source_viewer_loading),
                testTag = SOURCE_LOADING_TEST_TAG,
            )
            state.unavailable -> TaffyEmptyState(
                title = taffyString(R.string.taffy_workspace_unavailable_title),
                body = taffyString(R.string.taffy_workspace_unavailable_body),
                leading = { WorkspaceEmptyGlyph(TaffyIcon.GlobeSimple) },
            )
            state.missing -> TaffyEmptyState(
                title = taffyString(R.string.taffy_source_viewer_missing_title),
                body = taffyString(R.string.taffy_source_viewer_missing_body),
                leading = { WorkspaceEmptyGlyph(TaffyIcon.GlobeSimple) },
            )
            else -> {
                TaffyGroupedCard {
                    SourceViewerPair(
                        label = taffyString(R.string.taffy_source_viewer_host),
                        value = state.host,
                    )
                    WorkspaceCardHairline()
                    SourceViewerPair(
                        label = taffyString(R.string.taffy_source_viewer_read),
                        value = DateUtils
                            .getRelativeTimeSpanString(state.readAtEpochMillis)
                            .toString(),
                    )
                    WorkspaceCardHairline()
                    SourceViewerPair(
                        label = taffyString(R.string.taffy_source_viewer_facts),
                        value = taffyCount(state.factCount),
                    )
                    WorkspaceCardHairline()
                    SourceViewerPair(
                        label = taffyString(R.string.taffy_source_viewer_method),
                        value = taffyString(R.string.taffy_source_viewer_method_value),
                    )
                    WorkspaceCardHairline()
                    SourceViewerPair(
                        label = taffyString(R.string.taffy_source_viewer_in_scope),
                        value = taffyString(
                            if (state.excluded) {
                                R.string.taffy_source_viewer_excluded
                            } else {
                                R.string.taffy_source_viewer_included
                            },
                        ),
                    )
                }
                TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_source_viewer_open_live),
                    onClick = { onIntent(SourceViewerIntent.OpenLivePage) },
                    testTag = OPEN_LIVE_TEST_TAG,
                    icon = TaffyIcon.ArrowUpRight,
                )
            }
        }
    }
}

@Composable
private fun SourceViewerPair(label: String, value: String) {
    TaffyKeyValueRow(
        label = label,
        value = value,
        modifier = Modifier
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            ),
    )
}

/** The tag screen SCR-306's semantics tests name. */
const val OPEN_LIVE_TEST_TAG: String = "source_viewer_open_live"
const val SOURCE_LOADING_TEST_TAG: String = "source_viewer_loading"
