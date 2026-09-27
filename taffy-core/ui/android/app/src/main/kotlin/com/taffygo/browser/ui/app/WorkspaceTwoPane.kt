// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyDestinationGroups
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPaneSplit
import com.taffygo.browser.ui.core.ui.TaffyTwoPane
import com.taffygo.browser.ui.feature.workspaces.ExportSheetScreen
import com.taffygo.browser.ui.feature.workspaces.FactCorrectionScreen
import com.taffygo.browser.ui.feature.workspaces.SourceViewerScreen
import com.taffygo.browser.ui.feature.workspaces.WorkspaceDetailScreen
import com.taffygo.browser.ui.feature.workspaces.WorkspaceListScreen
import com.taffygo.browser.ui.feature.workspaces.WorkspacePanePlaceholder

/**
 * Workspaces as a list-detail pair: the list stays visible while a workspace,
 * a source, a correction or an export is open.
 */
@Composable
fun WorkspaceTwoPane(
    destination: TaffyDestination,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    TaffyTwoPane(
        primary = {
            WorkspaceListScreen(
                navigator = navigator,
                selectedWorkspaceId = TaffyDestinationGroups.workspaceId(destination),
                onBack = { navigator.popWhile(TaffyDestinationGroups::isWorkspace) },
            )
        },
        secondary = {
            when (destination) {
                TaffyDestination.WorkspaceList -> WorkspacePanePlaceholder()
                is TaffyDestination.WorkspaceDetail ->
                    WorkspaceDetailScreen(destination, navigator, showUp = false)
                is TaffyDestination.SourceViewer ->
                    SourceViewerScreen(destination, navigator, showUp = false)
                is TaffyDestination.FactCorrection ->
                    FactCorrectionScreen(destination, navigator, showUp = false)
                is TaffyDestination.ExportSheet ->
                    ExportSheetScreen(destination, navigator, showUp = false)
                else -> WorkspacePanePlaceholder()
            }
        },
        split = TaffyPaneSplit.LIST_DETAIL,
        modifier = modifier,
    )
}
