// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.browser.BrowserRepository
import org.chromium.base.Callback
import org.chromium.chrome.browser.tab.Tab
import org.chromium.taffy.browser.TaffyTaskSourceSelectionBridge
import org.chromium.taffy.shell.ChromiumTaskTabController

/**
 * The narrow Android product implementation of browser-owned task actions.
 *
 * Native chooses the exact product window and binds task/action/tab identities. This adapter owns
 * only the ordinary product mechanics after that decision: selected-engine resolution, the
 * pre-publication task-tab hook, exact navigation, and exact regular-tab closure.
 *
 * The tab work is asked of [ChromiumTaskTabController] directly rather than of the window's
 * mediator. None of these calls is on `BrowserMediator` — they are browser-owned task mechanics,
 * not a seam a Taffy surface may use — so routing them through the mediator only put a second name
 * on each one and made the mediator look like their owner.
 */
internal class TaffyTaskBrowserActions(
    private val registration: TaffyTaskSourceSelectionBridge,
    private val browser: BrowserRepository,
    private val taskTabs: ChromiumTaskTabController,
) : TaffyTaskSourceSelectionBridge.BrowserActions {
    override fun resolveSearchAddress(query: String): String? =
        browser.resolveSearchAddress(query)

    override fun openTaskTab(
        taskId: String,
        actionId: String,
        destinationAddress: String,
    ): Tab? = taskTabs.openTaskTab(destinationAddress) { tab ->
        registration.claimAssistantCreatedTaskTab(taskId, actionId, tab)
    }

    override fun openTaskDiscoveryTab(
        taskId: String,
        effectId: String,
        callback: Callback<Tab?>,
    ) {
        taskTabs.openTaskDiscoveryTab(
            { tab -> registration.claimAssistantCreatedTaskTab(taskId, effectId, tab) },
            callback,
        )
    }

    override fun startSearch(tab: Tab, query: String, destinationAddress: String): Boolean =
        browser.resolveSearchAddress(query) == destinationAddress &&
            taskTabs.startTaskSearch(tab, destinationAddress)

    override fun activateTaskTab(tab: Tab): Boolean = taskTabs.activateTaskTab(tab)

    override fun closeTaskTab(tab: Tab): Boolean = taskTabs.closeTaskTab(tab)
}
