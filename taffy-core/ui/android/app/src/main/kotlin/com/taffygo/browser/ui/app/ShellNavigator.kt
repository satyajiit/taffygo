// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator

/**
 * The navigation contract, over the shell's own back stack.
 *
 * A feature holds [TaffyNavigator] and a destination; this is the only place
 * that knows how a destination becomes a screen change, which is what keeps
 * cross-feature navigation from becoming a cross-feature dependency.
 */
internal class ShellNavigator(
    private val current: () -> BackStack,
    private val update: (BackStack) -> Unit,
    private val onLeaveToBackground: () -> Boolean = { false },
) : TaffyNavigator {

    override fun goTo(destination: TaffyDestination) {
        update(current().push(destination))
    }

    override fun replaceCurrent(destination: TaffyDestination) {
        update(current().replaceCurrent(destination))
    }

    override fun goBack(): Boolean {
        val stack = current()
        if (!stack.canGoBack) return false
        update(stack.pop())
        return true
    }

    override fun leaveToBackground(): Boolean = onLeaveToBackground()

    override fun goHome() {
        update(current().home())
    }

    override fun restart(destination: TaffyDestination) {
        update(current().replaceAll(destination))
    }

    override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) {
        update(current().popWhile(shouldPop))
    }
}
