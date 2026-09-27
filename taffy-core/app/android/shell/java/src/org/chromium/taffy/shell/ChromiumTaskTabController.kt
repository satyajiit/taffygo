// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import org.chromium.base.Callback
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tab.TabLaunchType
import org.chromium.chrome.browser.tab.TabSelectionType
import org.chromium.chrome.browser.tabmodel.TabClosureParams
import org.chromium.chrome.browser.tabmodel.TabCreatorManager
import org.chromium.chrome.browser.tabmodel.TabModel
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.content_public.browser.LoadUrlParams
import org.chromium.ui.base.PageTransition
import org.chromium.url.GURL

/** Exact-address creation, activation, search, and teardown for task-owned regular tabs. */
internal class ChromiumTaskTabController(
    private val selector: TabModelSelector,
    private val tabCreators: TabCreatorManager,
    private val startPageUrl: String,
) {
    fun openStoredPage(address: String): Boolean {
        ThreadUtils.assertOnUiThread()
        val exact = exactHttpAddress(address) ?: return false
        return tabCreators
            .getTabCreator(/* incognito= */ false)
            .launchUrl(exact.spec, TabLaunchType.FROM_CHROME_UI) != null
    }

    fun openTaskTab(address: String, claimBeforePublish: (Tab) -> Boolean): Tab? {
        ThreadUtils.assertOnUiThread()
        if (exactHttpAddress(address) == null) return null
        val creator = tabCreators.getTabCreator(/* incognito= */ false) as? TaffyTabCreator
            ?: return null
        return creator.createTaskOwnedTab(address) { tab -> claimBeforePublish(tab) }
    }

    fun openTaskDiscoveryTab(
        claimBeforePublish: (Tab) -> Boolean,
        callback: Callback<Tab?>,
    ) {
        ThreadUtils.assertOnUiThread()
        val creator = tabCreators.getTabCreator(/* incognito= */ false) as? TaffyTabCreator
        if (creator == null) {
            callback.onResult(null)
            return
        }
        creator.createTaskOwnedDiscoveryTab({ tab -> claimBeforePublish(tab) }, callback)
    }

    fun startTaskSearch(tab: Tab, address: String): Boolean {
        ThreadUtils.assertOnUiThread()
        if (tab.isDestroyed || tab.isOffTheRecord || exactHttpAddress(address) == null) return false
        val model = selector.getModel(/* incognito= */ false)
        if (model.indexOf(tab) == TabModel.INVALID_TAB_INDEX) return false
        tab.loadUrl(LoadUrlParams(address, PageTransition.GENERATED))
        return true
    }

    fun activateTaskTab(tab: Tab): Boolean {
        ThreadUtils.assertOnUiThread()
        if (tab.isDestroyed || tab.isOffTheRecord) return false
        val model = selector.getModel(/* incognito= */ false)
        val index = model.indexOf(tab)
        if (index == TabModel.INVALID_TAB_INDEX) return false
        selector.selectModel(/* incognito= */ false)
        model.setIndex(index, TabSelectionType.FROM_USER)
        return selector.currentTab === tab
    }

    fun closeTaskTab(tab: Tab): Boolean {
        ThreadUtils.assertOnUiThread()
        if (tab.isDestroyed || tab.isOffTheRecord) return false
        val model = selector.getModel(/* incognito= */ false)
        if (model.indexOf(tab) == TabModel.INVALID_TAB_INDEX) return false
        if (model.count == 1) {
            val replacement = tabCreators.getTabCreator(/* incognito= */ false)
                .launchUrl(startPageUrl, TabLaunchType.FROM_CHROME_UI)
            if (replacement == null || replacement === tab) return false
        }
        model.tabRemover.closeTabs(
            TabClosureParams.closeTab(tab).allowUndo(false).build(),
            /* allowDialog= */ false,
        )
        return true
    }

    private fun exactHttpAddress(address: String): GURL? {
        val parsed = GURL(address)
        return parsed.takeIf {
            it.isValid &&
                (it.scheme == "http" || it.scheme == "https") &&
                it.username.isEmpty() &&
                it.password.isEmpty() &&
                it.spec == address
        }
    }
}
