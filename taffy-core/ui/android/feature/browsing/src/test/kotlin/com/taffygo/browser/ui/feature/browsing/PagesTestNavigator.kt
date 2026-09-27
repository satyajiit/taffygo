// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator

/** Records where History and Bookmarks sent the person. */
class PagesTestNavigator : TaffyNavigator {
    val visited = mutableListOf<TaffyDestination>()
    val replaced = mutableListOf<TaffyDestination>()
    var backs: Int = 0

    override fun goTo(destination: TaffyDestination) {
        visited += destination
    }

    override fun replaceCurrent(destination: TaffyDestination) {
        visited += destination
        replaced += destination
    }

    override fun goBack(): Boolean {
        backs += 1
        return true
    }

    override fun goHome() = Unit
    override fun restart(destination: TaffyDestination) = Unit
    override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
}
