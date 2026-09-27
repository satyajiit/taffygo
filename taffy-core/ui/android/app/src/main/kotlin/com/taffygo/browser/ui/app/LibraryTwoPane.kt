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
import com.taffygo.browser.ui.feature.workspaces.KeepThisScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryCollectionScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryExportSheetScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryHomeScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryItemScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryPanePlaceholder

/**
 * Library as a list-detail pair: home stays visible while a collection, item,
 * or Keep this is open.
 */
@Composable
fun LibraryTwoPane(
    destination: TaffyDestination,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val selectedCollectionId = when (destination) {
        is TaffyDestination.LibraryCollection -> destination.collectionId
        is TaffyDestination.LibraryExport -> destination.collectionId
        is TaffyDestination.LibraryItem -> destination.collectionId
        else -> null
    }
    val selectedItemId = (destination as? TaffyDestination.LibraryItem)?.itemId

    TaffyTwoPane(
        primary = {
            LibraryHomeScreen(
                navigator = navigator,
                selectedCollectionId = selectedCollectionId,
                onBack = { navigator.popWhile(TaffyDestinationGroups::isLibrary) },
            )
        },
        secondary = {
            when (destination) {
                TaffyDestination.LibraryHome -> LibraryPanePlaceholder()
                is TaffyDestination.LibraryCollection ->
                    LibraryCollectionScreen(
                        destination = destination,
                        navigator = navigator,
                        selectedItemId = selectedItemId,
                        showUp = false,
                    )
                is TaffyDestination.LibraryExport ->
                    LibraryExportSheetScreen(destination, navigator, showUp = false)
                is TaffyDestination.LibraryItem ->
                    LibraryItemScreen(destination, navigator, showUp = false)
                TaffyDestination.KeepThis ->
                    KeepThisScreen(navigator = navigator, showUp = false)
                else -> LibraryPanePlaceholder()
            }
        },
        split = TaffyPaneSplit.LIST_DETAIL,
        modifier = modifier,
    )
}
