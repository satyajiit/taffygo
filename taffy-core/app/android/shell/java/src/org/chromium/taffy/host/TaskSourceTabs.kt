// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import org.chromium.chrome.browser.tab.Tab

/**
 * Which of a product window's tabs the task-source registry hears about.
 *
 * One window's selector holds both tab models, the person's own and the private
 * one, and [TaffySelectorTabBinding] and [TaffySelectedTabRouter] see every tab
 * in both. The registry they feed is the regular profile's: native refuses a tab
 * from any other profile, so asking it to register or select a private tab was
 * always going to answer no — and both callers treat a no as a broken invariant,
 * which is how opening a private tab from the tab switcher closed the browser.
 *
 * The rule is decision 0035's: a private tab is not a source. It is never
 * offered, counted or sent with a task, so it never enters the registry, and
 * while one is in front the registry holds no selection at all. The private tab
 * still gets its own profile's tab graph; it is only kept out of this one.
 */
object TaskSourceTabs {

    /** Whether [tab] may be registered with the regular profile's task-source registry. */
    @JvmStatic
    fun entersRegistry(tab: Tab): Boolean = !tab.isOffTheRecord

    /**
     * What the registry is told is selected when [tab] is: the tab itself, or
     * nothing while a private tab is in front. Leaving the last regular tab
     * selected behind a private one would let "this page" name a page the
     * person is not looking at.
     */
    @JvmStatic
    fun selection(tab: Tab?): Tab? = tab?.takeIf(::entersRegistry)
}
