// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * Coalesces browser-engine callbacks into the smallest required UI projection refresh.
 *
 * Chromium commonly reports one navigation as a burst of URL, title, loading,
 * and selection callbacks. Projecting every browser fact from every callback
 * repeats JNI reads and list construction while only the final state can be
 * drawn. This profile-owned helper retains the union of dirty projections and
 * publishes it once through the scheduler supplied by the platform adapter.
 *
 * All methods are called on the adapter's UI thread.
 */
class BrowserProjectionInvalidator(
    private val schedule: (Runnable) -> Unit,
    private val cancel: (Runnable) -> Unit,
    private val publish: (Scope) -> Unit,
) {
    private var pending = Scope.NONE
    private var scheduled = false
    private var closed = false
    private val publishPending = Runnable {
        scheduled = false
        val scope = pending
        pending = Scope.NONE
        if (!closed && scope != Scope.NONE) publish(scope)
    }

    fun tabsChanged() = invalidate(Scope.TABS)

    fun navigationChanged() = invalidate(Scope.NAVIGATION)

    fun tabsAndNavigationChanged() = invalidate(Scope.TABS_AND_NAVIGATION)

    fun filteringAndNavigationChanged() = invalidate(Scope.FILTERING_AND_NAVIGATION)

    fun everythingChanged() = invalidate(Scope.ALL)

    /** Cancels the queued publication and permanently ignores later callbacks. */
    fun close() {
        if (closed) return
        closed = true
        pending = Scope.NONE
        if (scheduled) {
            scheduled = false
            cancel(publishPending)
        }
    }

    private fun invalidate(scope: Scope) {
        if (closed) return
        pending = pending.merge(scope)
        if (scheduled) return
        scheduled = true
        schedule(publishPending)
    }

    /** Which independently expensive projections one publication must rebuild. */
    data class Scope(
        val tabs: Boolean,
        val navigation: Boolean,
        val filtering: Boolean,
    ) {
        internal fun merge(other: Scope) = Scope(
            tabs = tabs || other.tabs,
            navigation = navigation || other.navigation,
            filtering = filtering || other.filtering,
        )

        companion object {
            internal val NONE = Scope(tabs = false, navigation = false, filtering = false)
            internal val TABS = Scope(tabs = true, navigation = false, filtering = false)
            internal val NAVIGATION = Scope(tabs = false, navigation = true, filtering = false)
            internal val TABS_AND_NAVIGATION =
                Scope(tabs = true, navigation = true, filtering = false)
            internal val FILTERING_AND_NAVIGATION =
                Scope(tabs = false, navigation = true, filtering = true)
            val ALL = Scope(tabs = true, navigation = true, filtering = true)
        }
    }
}
