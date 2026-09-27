// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
package org.chromium.taffy.shell

import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tab.TabSelectionType
import org.chromium.chrome.browser.tabmodel.TabList
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelUtils
import com.taffygo.browser.ui.core.model.TabId

/** Checks current native attribution when a task card opens its page. */
object TaskTabSelection {
    data class Attribution(
        val assistantCreated: (Tab) -> Boolean,
        val creatingTaskId: (Tab) -> String,
        val acceptedSource: (String, Tab) -> Boolean,
    )

    fun select(
        selector: TabModelSelector,
        tab: Tab?,
        taskId: String,
        assistantCreated: ((Tab) -> Boolean)?,
        creatingTaskId: ((Tab) -> String)?,
        acceptedSource: ((String, Tab) -> Boolean)?,
    ): Boolean {
        if (tab == null || taskId.isBlank() || tab.isOffTheRecord) return false
        val created = assistantCreated?.invoke(tab) == true && creatingTaskId?.invoke(tab) == taskId
        if (!created && acceptedSource?.invoke(taskId, tab) != true) return false
        return selectCurrentTab(selector, tab)
    }

    /** Selects the actual current model item after the caller has decided whether it may. */
    fun selectCurrentTab(selector: TabModelSelector, tab: Tab?): Boolean {
        if (tab == null) return false
        val model = selector.getModel(tab.isOffTheRecord)
        val index = TabModelUtils.getTabIndexById(model, tab.id)
        if (index == TabList.INVALID_TAB_INDEX) return false
        selector.selectModel(tab.isOffTheRecord)
        model.setIndex(index, TabSelectionType.FROM_USER)
        return selector.currentTab === tab
    }

    /** Re-reads native consent for each current regular tab; no source membership is cached. */
    fun sources(
        selector: TabModelSelector,
        taskId: String,
        acceptedSource: ((String, Tab) -> Boolean)?,
    ): Set<TabId> {
        if (taskId.isBlank() || acceptedSource == null) return emptySet()
        val model = selector.getModel(false)
        return buildSet {
            for (index in 0 until model.count) {
                val tab = model.getTabAt(index) ?: continue
                if (!tab.isOffTheRecord && acceptedSource(taskId, tab)) add(TabId(tab.id.toString()))
            }
        }
    }
}
