// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.chrome.browser.tab.Tab
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.mockito.Mockito.`when`

/**
 * A private tab never reaches the regular profile's task-source registry.
 *
 * WHAT THIS SUITE IS FOR. Opening a private tab from the tab switcher closed the
 * browser: the window's selector reported the new private tab, the binding asked
 * the regular registry to take it, native refused a tab from another profile,
 * and the binding read the refusal as a broken invariant. Decision 0035 already
 * said what the answer is — a private tab is not a source — so these are that
 * rule's two questions: is it registered, and what is selected while it is in
 * front.
 *
 * WHAT IT CANNOT PROVE. That the private tab then opens and browses. That is the
 * tab model's and the private profile's, and the evidence for it is a run on a
 * device.
 */
@RunWith(BaseRobolectricTestRunner::class)
class TaskSourceTabsTest {

    @Test
    fun `a private tab never enters the registry`() {
        assertFalse(TaskSourceTabs.entersRegistry(tab(isPrivate = true)))
    }

    @Test
    fun `one of the person's own tabs does`() {
        assertTrue(TaskSourceTabs.entersRegistry(tab(isPrivate = false)))
    }

    @Test
    fun `a private tab in front leaves nothing selected`() {
        assertNull(TaskSourceTabs.selection(tab(isPrivate = true)))
    }

    @Test
    fun `the person's own tab in front is what is selected`() {
        val own = tab(isPrivate = false)
        assertSame(own, TaskSourceTabs.selection(own))
    }

    @Test
    fun `no tab in front is no selection`() {
        assertNull(TaskSourceTabs.selection(null))
    }

    private fun tab(isPrivate: Boolean): Tab = mock(Tab::class.java).also {
        `when`(it.isOffTheRecord).thenReturn(isPrivate)
    }
}
