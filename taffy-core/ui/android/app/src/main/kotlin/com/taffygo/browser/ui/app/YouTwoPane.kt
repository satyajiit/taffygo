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
import com.taffygo.browser.ui.feature.settings.MemoryScreen
import com.taffygo.browser.ui.feature.settings.SavedDetailsScreen
import com.taffygo.browser.ui.feature.settings.SavedSignInsScreen
import com.taffygo.browser.ui.feature.settings.TimeOnSitesScreen
import com.taffygo.browser.ui.feature.settings.WhatHappenedScreen
import com.taffygo.browser.ui.feature.settings.YouPanePlaceholder
import com.taffygo.browser.ui.feature.settings.YouScreen

/**
 * You as a list-detail pair: the hub stays visible while a child is open.
 */
@Composable
fun YouTwoPane(
    destination: TaffyDestination,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    TaffyTwoPane(
        primary = {
            YouScreen(
                navigator = navigator,
                onBack = { navigator.popWhile(TaffyDestinationGroups::isYou) },
            )
        },
        secondary = {
            when (destination) {
                TaffyDestination.You -> YouPanePlaceholder()
                TaffyDestination.TimeOnSites ->
                    TimeOnSitesScreen(navigator = navigator, showUp = false)
                TaffyDestination.WhatHappened ->
                    WhatHappenedScreen(navigator = navigator, showUp = false)
                TaffyDestination.SavedSignIns ->
                    SavedSignInsScreen(navigator = navigator, showUp = false)
                TaffyDestination.SavedDetails ->
                    SavedDetailsScreen(navigator = navigator, showUp = false)
                TaffyDestination.Memory ->
                    MemoryScreen(navigator = navigator, showUp = false)
                else -> YouPanePlaceholder()
            }
        },
        split = TaffyPaneSplit.LIST_DETAIL,
        modifier = modifier,
    )
}
