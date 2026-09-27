// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * The one recorder of visits: watches the tab list and counts host commits.
 *
 * A visit is a tab arriving somewhere it was not — the host of a tab changing
 * between two observed snapshots. The first snapshot is a baseline and counts
 * nothing: those tabs were already open when this window appeared (a restored
 * session, or a second window over the same profile), and counting a tab for
 * existing would add a visit per restart to every site the person merely
 * keeps open.
 *
 * The exclusions are [FrequentSitesRepository]'s own: a private tab leaves no
 * trace, a Taffy tab is not the person's habit, and a tab on a blank host has
 * not been anywhere. They are checked at recording time, not at baseline
 * time, so a private tab's host never reaches the recorder at all.
 */
internal class FrequentSitesTracker(
    private val tabs: StateFlow<List<Tab>>,
    private val recorder: FrequentSitesRepository,
) {

    /** Watch until [scope] is cancelled, which is the owning window closing. */
    fun start(scope: CoroutineScope) {
        scope.launch {
            var seen: Map<TabId, String>? = null
            tabs.collect { current ->
                val previous = seen
                seen = current.associate { tab -> tab.id to tab.host }
                if (previous == null) return@collect
                current.forEach { tab ->
                    if (countable(tab) && previous[tab.id] != tab.host) {
                        recorder.recordVisit(tab.host, tab.title)
                    }
                }
            }
        }
    }

    private fun countable(tab: Tab): Boolean =
        !tab.isPrivate && !tab.isTaffyTab && tab.host.isNotBlank()
}
