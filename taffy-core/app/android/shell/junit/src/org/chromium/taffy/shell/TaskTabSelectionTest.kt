// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.model.TabId
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tab.TabSelectionType
import org.chromium.chrome.browser.tabmodel.TabModel
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.mockito.Mockito.never
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`

@RunWith(BaseRobolectricTestRunner::class)
class TaskTabSelectionTest {
    @Test
    fun currentConsentCanDisplayAndOpenPersonTabsWithoutCreatingOwnership() {
        val selector = mock(TabModelSelector::class.java)
        val model = mock(TabModel::class.java)
        val first = tab(17)
        val second = tab(18)
        val privateTab = tab(19, isPrivate = true)
        `when`(selector.getModel(false)).thenReturn(model)
        `when`(model.count).thenReturn(3)
        `when`(model.getTabAt(0)).thenReturn(first)
        `when`(model.getTabAt(1)).thenReturn(second)
        `when`(model.getTabAt(2)).thenReturn(privateTab)
        `when`(model.getTabById(17)).thenReturn(first)
        `when`(model.indexOf(first)).thenReturn(0)
        `when`(selector.currentTab).thenReturn(first)
        var accepted = setOf(17, 18, 19)
        val currentSource: (String, Tab) -> Boolean = { task, candidate ->
            task == "comparison" && candidate.id in accepted
        }
        assertEquals(setOf(TabId("17"), TabId("18")),
            TaskTabSelection.sources(selector, "comparison", currentSource))
        assertTrue(TaskTabSelection.select(selector, first, "comparison", { false }, { "" }, currentSource))
        verify(model).setIndex(0, TabSelectionType.FROM_USER)

        accepted = emptySet()
        assertEquals(emptySet<TabId>(), TaskTabSelection.sources(selector, "comparison", currentSource))
        assertFalse(TaskTabSelection.select(selector, second, "comparison", { false }, { "" }, currentSource))
        verify(model, never()).setIndex(1, TabSelectionType.FROM_USER)
        assertFalse(TaskTabSelection.select(selector, privateTab, "comparison", { true }, { "comparison" }, currentSource))
    }

    private fun tab(id: Int, isPrivate: Boolean = false): Tab = mock(Tab::class.java).also {
        `when`(it.id).thenReturn(id)
        `when`(it.isOffTheRecord).thenReturn(isPrivate)
    }
}
